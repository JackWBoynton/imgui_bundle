
#include "imgrid.h"
#include "imgrid_internal.h"

#include <limits.h>
#include <math.h>
#include <new>
#include <optional>
#include <stdint.h>
#include <stdio.h> // for fwrite, ssprintf, sscanf
#include <stdlib.h>
#include <string.h> // strlen, strncmp

// Use secure CRT function variants to avoid MSVC compiler errors
#ifdef _MSC_VER
#define sscanf sscanf_s
#endif

ImGridContext *GImGrid = NULL;

ImGridIO::MultipleSelectModifier::MultipleSelectModifier() : Modifier(NULL) {}

ImGridStyle::ImGridStyle()
    : GridSpacing(24.f), EntryCornerRounding(4.f), EntryPadding(8.f, 8.f),
      EntryBorderThickness(1.f), Flags(ImGridStyleFlags_None), Colors() {}

namespace ImGrid {

namespace {

void Initialize(ImGridContext *ctx) {
  ctx->HoveredEntryIdx = -1;
  ctx->HoveredEntryTitleBarIdx = -1;
  ctx->CurrentScope = ImGridScope_None;

  StyleColorsDark();
}

inline ImVec2 ScreenSpaceToGridSpace(const ImGridContext &ctx,
                                     const ImVec2 &v) {
  return v - ctx.CanvasOriginScreenSpace - ctx.Panning;
}

inline ImRect ScreenSpaceToGridSpace(const ImGridContext &ctx,
                                     const ImRect &r) {
  return ImRect(ScreenSpaceToGridSpace(ctx, r.Min),
                ScreenSpaceToGridSpace(ctx, r.Max));
}

inline ImVec2 GridSpaceToScreenSpace(const ImGridContext &ctx,
                                     const ImVec2 &v) {
  return v + ctx.CanvasOriginScreenSpace + ctx.Panning;
}

inline ImVec2 GridSpaceToSpace(const ImGridContext &ctx, const ImVec2 &v) {
  return v + ctx.Panning;
}

inline ImVec2 SpaceToGridSpace(const ImGridContext &ctx, const ImVec2 &v) {
  return v - ctx.Panning;
}

inline ImVec2 SpaceToScreenSpace(const ImVec2 &v) {
  return GImGrid->CanvasOriginScreenSpace + v;
}

inline ImVec2 GetEntryTitleBarOrigin(const ImGridEntryData &node) {
  return node.Origin + node.LayoutStyle.Padding;
}

inline ImRect GetItemRect() {
  return ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

inline ImVec2 GetEntryContentOrigin(const ImGridEntryData &node) {
  const ImVec2 title_bar_height =
      ImVec2(0.f, node.TitleBarContentRect.GetHeight() +
                      2.0f * node.LayoutStyle.Padding.y);
  return node.Origin + title_bar_height + node.LayoutStyle.Padding;
}

inline ImRect GetEntryTitleRect(const ImGridEntryData &node) {
  ImRect expanded_title_rect = node.TitleBarContentRect;
  expanded_title_rect.Expand(node.LayoutStyle.Padding);

  return ImRect(expanded_title_rect.Min,
                expanded_title_rect.Min + ImVec2(node.Rect.GetWidth(), 0.f) +
                    ImVec2(0.f, expanded_title_rect.GetHeight()));
}

inline bool RectsAreTouching(ImRect a, ImRect b) {
  b.Expand(ImVec2(1, 1));
  return a.Overlaps(b);
}

inline bool SwapEntryPositions(ImGridEntryData &a, ImGridEntryData &b) {
  if (a.Locked || b.Locked)
    return false;

  auto swapper = [&]() {
    auto x = b.GridPosition.x;
    auto y = b.GridPosition.y;
    b.GridPosition.x = a.GridPosition.x;
    b.GridPosition.y = a.GridPosition.y; // b -> a position
    if (a.GridPosition.h != b.GridPosition.h) {
      a.GridPosition.x = x;
      a.GridPosition.y =
          b.GridPosition.y + b.GridPosition.h; // a -> goes after b
    } else if (a.GridPosition.w != b.GridPosition.w) {
      a.GridPosition.x = b.GridPosition.x + b.GridPosition.w;
      a.GridPosition.y = y; // a -> goes after b
    } else {
      a.GridPosition.x = x;
      a.GridPosition.y = y; // a -> old b position
    }
    return true;
  };

  std::optional<bool> touching = false;
  // same size and same row or column, and touching
  if (a.GridPosition.w == b.GridPosition.w &&
      a.GridPosition.h == b.GridPosition.h &&
      (a.GridPosition.x == b.GridPosition.x ||
       a.GridPosition.y == b.GridPosition.y))
    if (RectsAreTouching(a.Rect, b.Rect))
      return swapper();
  if (touching.has_value() && !touching.value())
    return false; // IFF ran test and fail, bail out

  // check for taking same columns (but different height) and touching
  if (a.GridPosition.w == b.GridPosition.w &&
      a.GridPosition.x == b.GridPosition.x &&
      (touching.value_or(false) || (RectsAreTouching(a.Rect, b.Rect)))) {
    return swapper();
  }
  if (!touching.value_or(false))
    return false;

  // check if taking same row (but different width) and touching
  if (a.GridPosition.h == b.GridPosition.h &&
      a.GridPosition.y == b.GridPosition.y &&
      (touching || (RectsAreTouching(a.Rect, b.Rect)))) {
    return swapper();
  }
  return false;
}

void PackGrid() {}

inline bool GridPositionsAreIntercepted(ImGridPosition a, ImGridPosition b) {
  return !(a.y >= b.y + b.h || a.y + a.h <= b.y || a.x + a.w <= b.x ||
           a.x >= b.x + b.w);
}

bool GridFindEmptyPosition(ImGridEntryData &entry, int column,
                           ImGridEntryData *after) {
  int start = 0;
  if (after != NULL)
    start = after->GridPosition.y * column +
            (after->GridPosition.x + after->GridPosition.w);

  bool found = false;
  for (int i = start; !found; ++i) {
    int x = i % column;
    int y = i / column;
    if (x + entry.GridPosition.w > column)
      continue;
    ImGridPosition box = {x, y, entry.GridPosition.w, entry.GridPosition.h};
    bool intercepted = false;
    for (int j = 0; j < GImGrid->Entries.Pool.size(); ++j) {
      if (GridPositionsAreIntercepted(box,
                                      GImGrid->Entries.Pool[j].GridPosition)) {
        intercepted = true;
        break;
      }
    }
    if (!intercepted) {
      if (entry.x != x || entry.y != y)
        entry.Modified = true;

      node.x = x;
      node.y = y;
      found = true;
    }
  }
  return found;
}

ImGridEntryData *GridAddEntry(ImGridEntryData *entry,
                              bool trigger_add_event = false,
                              ImGridEntryData *after = NULL) {
  for (int i = 0; i < GImGrid->GridEntries.size(); i++) {
    if (GImGrid->GridEntries[i]->Id == entry->Id)
      return GImGrid->GridEntries[i];
  }

  GImGrid->GridInColumnResize ? GridEntryBoundFix(entry)
                              : GridPrepareEntry(entry);

  bool skip_collision = false;
  if (entry->AutoPosition &&
      GridFindEmptyPosition(*entry, GImGrid->GridColumn, after)) {
    entry->AutoPosition = false;
    skip_collision = true;
  }

  GImGrid->GridEntries.push_back(entry);
  if (trigger_add_event)
    GImGrid->GridAddedEntries.push_back(entry);

  if (!skip_collision)
    GridFixCollisions(entry);
  if (!GImGrid->GridBatchMode)
    GridPackEntries();
  return entry;
}

void GridRemoveEntry(ImGridEntryData *entry, bool trigger_event = false) {
  bool found = false;
  for (int i = 0; i < GImGrid->GridEntries.size(); i++) {
    if (GImGrid->GridEntries[i]->Id == entry->Id)
      found = true;
  }

  if (!found)
    return;

  if (trigger_event)
    GImGrid->GridRemovedEntries.push_back(entry);

  // TODO:
  //  don't use 'faster' .splice(findIndex(),1) in case node isn't in our list,
  //  or in multiple times.
  // this.nodes = this.nodes.filter(n => n._id !== node._id);
  // if (!node._isAboutToRemove) this._packNodes(); // if dragged out, no need
  // to relayout as already done... this._notify([node]);
}

/*
*   static sort(nodes: GridStackNode[], dir: 1 | -1 = 1): GridStackNode[] {
    const und = 10000;
    return nodes.sort((a, b) => {
      let diffY = dir * ((a.y ?? und) - (b.y ?? und));
      if (diffY === 0) return dir * ((a.x ?? und) - (b.x ?? und));
      return diffY;
    });
  }
*/

inline void GridSortNodes(ImVector<ImGridEntryData *>& nodes,)
                                                bool upwards) {
  int direction = upwards ? -1 : 1;
  int und = 10000;

  std::sort(
      nodes.begin(), nodes.end(), [&](ImGridEntryData *a, ImGridEntryData *b) {
        int diffY =
            direction * ((a->y == -1 ? und : a->y) - (b->y == -1 ? und : b->y));
        if (diffY == 0)
          return direction *
                 ((a->x == -1 ? und : a->x) - (b->x == -1 ? und : b->x));
        return diffY;
      });
}

bool GridEntryMoveCheck(ImGridEntryData *entry, ImGridMoveOptions opts) {}

bool GridMoveNode(ImGridEntryData *entry, ImGridMoveOptions opts) {
  if (entry == NULL)
    return false;
}

void GridColumnChanged(
    int previous_column, int column,
    ImGridColumnOptions opts = ImGridColumnOptions_MoveScale) {
  if (GImGrid->GridEntries.size() == 0 || previous_column == column)
    return;

  if (opts.Flags == ImGridColumnFlags_None)
    return;

  bool compact = opts.Flags & ImGridColumnFlags_Compact ||
                 opts.Flags & ImGridColumnFlags_List;
  if (compact)
    GridSortNodes(GImGrid->GridEntries, true);

  if (column < previous_column)
    GridCacheLayout(previous_column);
  GridBatchUpdate();

  ImVector<ImsGridEntryData *> new_entries;
  ImVector<ImGridEntryData *> ordered_entries =
      compact ? GImGrid->GridEntries
              : GridSortNodes(GImGrid->GridEntries, false);
  if (column > previous_column && GImGrid->GridCacheLayouts.size() >= column) {
    int last_index = GImGrid->GridCacheLayouts.size() - 1;
    const auto &cache = GImGrid->GridCacheLayouts[last_index];
    if (!cache.size() > 0 && previous_column != last_index &&
        GImGrid->GridCacheLayouts[last_index].size() > 0) {
      preview_column = last_index;
      for (int i = 0; i < cache.size(); ++i) {
        auto &entry = cache[i];
        ImGridEntryData *inner_entry = NULL;
        for (int node_ind = 0;
             node_ind < ordered_entries.size() && inner_entry == NULL;
             ++node_ind) {
          if (ordered_entries[node_ind]->Id == entry->Id)
            inner_entry = ordered_entries[node_ind];
        }

        if (inner_entry != NULL) {
          if (!compact && !inner_entry->AutoPosition) {
            inner_entry->x = entry.x;
            inner_entry->y = entry.y;
          }
          inner_entry->w = entry.w;
        }
      }
    }

    for (int i = 0; i < cache.size(); ++i) {
      ImGridEntryDataWrapper *entry = cache[i];
      int found_index = -1;
      for (int inner_ind = 0; inner_ind < ordered_entries.size(); ++inner_ind) {
        if (ordered_entries[inner_ind]->Id == entry.Parent->Id)
          found_index = inner_ind;
      }

      if (found_index >= 0) {
        ImGridEntryData *inner_entry = ordered_entries[found_index];
        if (compact) {
          inner_entry->w = entry.GridPosition.w;
          return;
        }
        if (entry->AutoPosition) {
          // TODO: this Wrapper might need to be updated to use the parent's
          // Position?
          GridFindEmptyPosition(*entry, new_entries);
        }

        if (!entry->AutoPosition) {
          inner_entry->x = entry.GridPosition.x;
          inner_entry->y = entry.GridPosition.y;
          inner_entry->w = entry.GridPosition.w;

          new_entries.push_back(inner_entry);
        }

        // remove found_index from ordered_entries
        ordered_entries.erase(ordered_entries.begin() + found_index);
      }
    }
  }

  if (compact) {
    GridCompact(opts);
  } else {
    if (ordered_entries.size() > 0) {
      if (opts.Func != NULL) {
        opts.Func(column, previous_column, new_entries, ordered_entries);
      } else {
        float ratio = compact ? 1 : column / previous_column;
        bool move = (opts.Flags & ImGridColumnFlags_Move) ||
                    (opts.Flags & ImGridColumnFlags_MoveScale);
        bool scale = (opts.Flags & ImGridColumnFlags_Scale) ||
                     (opts.Flags & ImGridColumnFlags_MoveScale);
        for (int i = 0; i < ordered_entries.size(); ++i) {
          auto &entry = ordered_entries[i];
          entry.x = (column == 1 ? 0
                                 : (move ? IM_ROUND(entry.x * ratio)
                                         : IM_MIN(entry.x, column - 1)));
          entry.w = ((column == 1 || previous_column == 1) ? 1
                     : scale ? (IM_ROUND(entry.w * ratio))
                             : IM_MIN(entry.w, column));
          new_entries.push_back(entry);
        }
        ordered_entries.clear();
      }
    }

    GridSortNodes(new_entries, false);
    GImGrid->GridInColumnResize = true;
    GImGrid->GridEntries.clear();
    for (int i = 0; i < new_entries.size(); ++i) {
      GridAddEntry(new_entries[i], false);
      new_entries[i]->PrevGridPosition.Reset();
    }
  }

  for (int i = 0; i < GImGrid->GridEntries.size(); ++i) {
    GImGrid->GridEntries[i]->PrevGridPosition.Reset();
  }
  GridBatchUpdate(false, !compact);
  GImGrid->GridInColumnResize = false;
}

void GridCacheLayout(ImVector<ImGridEntryData *> nodes, int column,
                     bool clear = false) {
  ImVector<ImGridEntryDataWrapper *> entries;
  for (int i = 0; i < nodes.size(); ++i) {
    auto &node = nodes[i];
    // TODO: this is gross as we are only overwriting the h
    entries.push_back(ImGridEntryDataWrapper{
        node, ImGridPosition{node->GridPosition.x, node->GridPosition.y,
                             node->GridPosition.w, -1}});
  }
  if (clear)
    GImGrid->GridEntries.clear();
  GImGrid->GridCacheLayouts[column] = entries;
}

int GridFindCacheLayout(ImGridEntryData *node, int column) {
  if (!GImGrid->GridCacheLayouts.contains(column))
    return -1;
  for (int i = 0; i < GImGrid->GridCacheLayouts[column].size(); ++i) {
    if (GImGrid->GridCacheLayouts[column][i]->Parent->Id == node->Id)
      return i;
  }
  return -1;
}

void GridCacheOneLayout(ImGridEntryData *node, int column) {
  ImGridEntryDataWrapper wrapped = {
      node, ImGridPosition{node->x, node->y, node->w, -1}};
  if (node->AutoPosition || node->x == -1) {
    node->GridPosition.x = -1;
    node->GridPosition.y = -1;
    if (node->AutoPosition)
      wrapped.AutoPosition = true;
  }

  if (!GImGrid->GridCacheLayouts.contains(column))
    GImGrid->GridCacheLayouts[column] = ImVector<ImGridEntryDataWrapper *>();
  int index = GridFindCacheLayout(node, column);
  if (index < 0)
    GImGrid->GridCacheLayouts[column].push_back(wrapped);
  else
    GImGrid->GridCacheLayouts[column][index] = wrapped;
}

// SECTION[DrawLists]
// The draw list channels are structured as follows. First we have our base
// channel, the canvas grid on which we render the grid lines in
// BeginNodeEditor(). The base channel is the reason
// draw_list_submission_idx_to_background_channel_idx offsets the index by
// one. Each BeginEntry() call appends two new draw channels, for the entry
// background and foreground. The node foreground is the channel into which
// the node's ImGui content is rendered. Finally, in EndNodeEditor() we append
// one last draw channel for rendering the selection box and the incomplete
// link on top of everything else.
//
// +----------+----------+----------+----------+----------+----------+
// |          |          |          |          |          |          |
// |canvas    |node      |node      |...       |...       |click     |
// |grid      |background|foreground|          |          |interaction
// |          |          |          |          |          |          |
// +----------+----------+----------+----------+----------+----------+
//            |                     |
//            |   submission idx    |
//            |                     |
//            -----------------------

void DrawListSet(ImDrawList *window_draw_list) {
  GImGrid->CanvasDrawList = window_draw_list;
  GImGrid->EntryIdxToSubmissionIdx.Clear();
  GImGrid->EntryIdxSubmissionOrder.clear();
}

void ImDrawListGrowChannels(ImDrawList *draw_list, const int num_channels) {
  ImDrawListSplitter &splitter = draw_list->_Splitter;

  if (splitter._Count == 1) {
    splitter.Split(draw_list, num_channels + 1);
    return;
  }

  // NOTE: this logic has been lifted from ImDrawListSplitter::Split with
  // slight modifications to allow nested splits. The main modification is
  // that we only create new ImDrawChannel instances after splitter._Count,
  // instead of over the whole splitter._Channels array like the regular
  // ImDrawListSplitter::Split method does.

  const int old_channel_capacity = splitter._Channels.Size;
  // NOTE: _Channels is not resized down, and therefore _Count <=
  // _Channels.size()!
  const int old_channel_count = splitter._Count;
  const int requested_channel_count = old_channel_count + num_channels;
  if (old_channel_capacity < old_channel_count + num_channels) {
    splitter._Channels.resize(requested_channel_count);
  }

  splitter._Count = requested_channel_count;

  for (int i = old_channel_count; i < requested_channel_count; ++i) {
    ImDrawChannel &channel = splitter._Channels[i];

    // If we're inside the old capacity region of the array, we need to reuse
    // the existing memory of the command and index buffers.
    if (i < old_channel_capacity) {
      channel._CmdBuffer.resize(0);
      channel._IdxBuffer.resize(0);
    }
    // Else, we need to construct new draw channels.
    else {
      IM_PLACEMENT_NEW(&channel) ImDrawChannel();
    }

    {
      ImDrawCmd draw_cmd;
      draw_cmd.ClipRect = draw_list->_ClipRectStack.back();
      draw_cmd.TextureId = draw_list->_TextureIdStack.back();
      channel._CmdBuffer.push_back(draw_cmd);
    }
  }
}

void ImDrawListSplitterSwapChannels(ImDrawListSplitter &splitter,
                                    const int lhs_idx, const int rhs_idx) {
  if (lhs_idx == rhs_idx) {
    return;
  }

  IM_ASSERT(lhs_idx >= 0 && lhs_idx < splitter._Count);
  IM_ASSERT(rhs_idx >= 0 && rhs_idx < splitter._Count);

  ImDrawChannel &lhs_channel = splitter._Channels[lhs_idx];
  ImDrawChannel &rhs_channel = splitter._Channels[rhs_idx];
  lhs_channel._CmdBuffer.swap(rhs_channel._CmdBuffer);
  lhs_channel._IdxBuffer.swap(rhs_channel._IdxBuffer);

  const int current_channel = splitter._Current;

  if (current_channel == lhs_idx) {
    splitter._Current = rhs_idx;
  } else if (current_channel == rhs_idx) {
    splitter._Current = lhs_idx;
  }
}

void DrawListAppendClickInteractionChannel() {
  // NOTE: don't use this function outside of EndNodeEditor. Using this before
  // all nodes have been added will screw up the node draw order.
  ImDrawListGrowChannels(GImGrid->CanvasDrawList, 1);
}

int DrawListSubmissionIdxToBackgroundChannelIdx(const int submission_idx) {
  // NOTE: the first channel is the canvas background, i.e. the grid
  return 1 + 2 * submission_idx;
}

int DrawListSubmissionIdxToForegroundChannelIdx(const int submission_idx) {
  return DrawListSubmissionIdxToBackgroundChannelIdx(submission_idx) + 1;
}

void DrawListActivateClickInteractionChannel() {
  GImGrid->CanvasDrawList->_Splitter.SetCurrentChannel(
      GImGrid->CanvasDrawList, GImGrid->CanvasDrawList->_Splitter._Count - 1);
}

void DrawListAddEntry(const int node_idx) {
  GImGrid->EntryIdxToSubmissionIdx.SetInt(
      static_cast<ImGuiID>(node_idx), GImGrid->EntryIdxSubmissionOrder.Size);
  GImGrid->EntryIdxSubmissionOrder.push_back(node_idx);
  ImDrawListGrowChannels(GImGrid->CanvasDrawList, 2);
}

void DrawListActivateCurrentEntryForeground() {
  const int foreground_channel_idx =
      DrawListSubmissionIdxToForegroundChannelIdx(
          GImGrid->EntryIdxSubmissionOrder.Size - 1);
  GImGrid->CanvasDrawList->_Splitter.SetCurrentChannel(GImGrid->CanvasDrawList,
                                                       foreground_channel_idx);
}

void DrawListActivateEntryBackground(const int node_idx) {
  const int submission_idx = GImGrid->EntryIdxToSubmissionIdx.GetInt(
      static_cast<ImGuiID>(node_idx), -1);
  // There is a discrepancy in the submitted node count and the rendered node
  // count! Did you call one of the following functions
  // * EditorContextMoveToNode
  // * SetNodeScreenSpacePos
  // * SetNodeGridSpacePos
  // * SetNodeDraggable
  // after the BeginNode/EndNode function calls?
  IM_ASSERT(submission_idx != -1);
  const int background_channel_idx =
      DrawListSubmissionIdxToBackgroundChannelIdx(submission_idx);
  GImGrid->CanvasDrawList->_Splitter.SetCurrentChannel(GImGrid->CanvasDrawList,
                                                       background_channel_idx);
}

void DrawListSwapSubmissionIndices(const int lhs_idx, const int rhs_idx) {
  IM_ASSERT(lhs_idx != rhs_idx);

  const int lhs_foreground_channel_idx =
      DrawListSubmissionIdxToForegroundChannelIdx(lhs_idx);
  const int lhs_background_channel_idx =
      DrawListSubmissionIdxToBackgroundChannelIdx(lhs_idx);
  const int rhs_foreground_channel_idx =
      DrawListSubmissionIdxToForegroundChannelIdx(rhs_idx);
  const int rhs_background_channel_idx =
      DrawListSubmissionIdxToBackgroundChannelIdx(rhs_idx);

  ImDrawListSplitterSwapChannels(GImGrid->CanvasDrawList->_Splitter,
                                 lhs_background_channel_idx,
                                 rhs_background_channel_idx);
  ImDrawListSplitterSwapChannels(GImGrid->CanvasDrawList->_Splitter,
                                 lhs_foreground_channel_idx,
                                 rhs_foreground_channel_idx);
}

void DrawListSortChannelsByDepth(const ImVector<int> &node_idx_depth_order) {
  if (GImGrid->EntryIdxToSubmissionIdx.Data.Size < 2) {
    return;
  }

  IM_ASSERT(node_idx_depth_order.Size == GImGrid->EntryIdxSubmissionOrder.Size);

  int start_idx = node_idx_depth_order.Size - 1;

  while (node_idx_depth_order[start_idx] ==
         GImGrid->EntryIdxSubmissionOrder[start_idx]) {
    if (--start_idx == 0) {
      // early out if submission order and depth order are the same
      return;
    }
  }

  // TODO: this is an O(N^2) algorithm. It might be worthwhile revisiting this
  // to see if the time complexity can be reduced.

  for (int depth_idx = start_idx; depth_idx > 0; --depth_idx) {
    const int node_idx = node_idx_depth_order[depth_idx];

    // Find the current index of the node_idx in the submission order array
    int submission_idx = -1;
    for (int i = 0; i < GImGrid->EntryIdxSubmissionOrder.Size; ++i) {
      if (GImGrid->EntryIdxSubmissionOrder[i] == node_idx) {
        submission_idx = i;
        break;
      }
    }
    IM_ASSERT(submission_idx >= 0);

    if (submission_idx == depth_idx) {
      continue;
    }

    for (int j = submission_idx; j < depth_idx; ++j) {
      DrawListSwapSubmissionIndices(j, j + 1);
      ImSwap(GImGrid->EntryIdxSubmissionOrder[j],
             GImGrid->EntryIdxSubmissionOrder[j + 1]);
    }
  }
}

bool MouseInCanvas() {
  // This flag should be true either when hovering or clicking something in
  // the canvas.
  const bool is_window_hovered_or_focused =
      ImGui::IsWindowHovered() || ImGui::IsWindowFocused();

  return is_window_hovered_or_focused &&
         GImGrid->CanvasRectScreenSpace.Contains(ImGui::GetMousePos());
}

void BeginCanvasInteraction() {
  const bool any_ui_element_hovered =
      GImGrid->HoveredEntryIdx.HasValue() || ImGui::IsAnyItemHovered();

  const bool mouse_not_in_canvas = !MouseInCanvas();

  if (GImGrid->ClickInteraction.Type != ImGridClickInteractionType_None ||
      any_ui_element_hovered || mouse_not_in_canvas) {
    return;
  }
}

ImGridRow *FindNearestInsertionPoint(ImVec2 insertion_size,
                                     ImVec2 *starting_point = NULL) {
  ImGridRow *output = NULL;
}

void SolveGrid() {
  // this function should reorder the grid my moving around entries Rects if
  // needed. it should also render a preview of the current dragged Entry of
  // where the entry would be placed if it were to be dropped
  //
  // only ever one Entry will have the HasPreview bool set, and if it is, find
  // the closest insertion point and set the PreviewRect of that node to the
  // correcct location
}

ImVec2 SnapOriginToGrid(ImVec2 origin) { return origin; }

void TranslateSelectedEntries() {
  if (!GImGrid->LeftMouseDragging)
    return;

  const ImVec2 origin =
      SnapOriginToGrid(GImGrid->MousePos - GImGrid->CanvasOriginScreenSpace -
                       GImGrid->Panning + GImGrid->PrimaryEntryOffset);

  for (int i = 0; i < GImGrid->SelectedEntryIndices.size(); ++i) {
    const ImVec2 entry_rel = GImGrid->SelectedEntryOffsets[i];
    const int entry_idx = GImGrid->SelectedEntryIndices[i];
    ImGridEntryData &entry = GImGrid->Entries.Pool[entry_idx];
    if (entry.Draggable)
      entry.Origin = origin + entry_rel;

    if (!entry.HasPreview) {
      entry.PreviewRect = entry.Rect;
      entry.HasPreview = true;
    }
  }

  // add a preview box where this will snap to if dropped
}

void ClickInteractionUpdate() {
  switch (GImGrid->ClickInteraction.Type) {

  case ImGridClickInteractionType_Entry: {
    TranslateSelectedEntries();
    if (GImGrid->LeftMouseReleased) {
      GImGrid->ClickInteraction.Type = ImGridClickInteractionType_None;
      for (int i = 0; i < GImGrid->SelectedEntryIndices.size(); ++i) {
        const int entry_idx = GImGrid->SelectedEntryIndices[i];
        ImGridEntryData &entry = GImGrid->Entries.Pool[entry_idx];
        entry.HasPreview = false;
      }
    }
    break;
  }
  case ImGridClickInteractionType_ImGuiItem: {
    if (GImGrid->LeftMouseReleased) {
      GImGrid->ClickInteraction.Type = ImGridClickInteractionType_None;
    }
    break;
  }
  case ImGridClickInteractionType_Resizing: {
    if (GImGrid->LeftMouseReleased)
      GImGrid->ClickInteraction.Type = ImGridClickInteractionType_None;
    break;
  }
  case ImGridClickInteractionType_None:
    break;
  }
}

void DrawEntryDecorations(ImGridContext &ctx, ImGridEntryData &entry) {
  if (entry.Resizable) {
    const ImRect resize_grabber_rect =
        ImRect(entry.Rect.Max - ImVec2(5, 5), entry.Rect.Max);

    // HACK: this ID is wrong
    ImGui::ButtonBehavior(resize_grabber_rect, entry.Id + 3,
                          &entry.PreviewHovered, &entry.PreviewHeld);
    if (entry.PreviewHeld || entry.PreviewHovered)
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
  }
}

void DrawEntryPreview(ImGridContext &ctx, const ImGridEntryData &entry) {
  ctx.CanvasDrawList->AddRect(
      entry.PreviewRect.Min, entry.PreviewRect.Max,
      entry.ColorStyle.PreviewOutline, entry.LayoutStyle.CornerRounding,
      ImDrawFlags_None, entry.LayoutStyle.BorderThickness);
  ctx.CanvasDrawList->AddRectFilled(
      entry.PreviewRect.Min, entry.PreviewRect.Max,
      entry.ColorStyle.PreviewFill, entry.LayoutStyle.CornerRounding);
}

void DrawEntry(ImGridContext &ctx, const int entry_idx) {
  ImGridEntryData &entry = ctx.Entries.Pool[entry_idx];

  ImGui::SetCursorPos(entry.Origin + GImGrid->Panning);
  ImU32 entry_background = entry.ColorStyle.Background;
  ImU32 titlebar_background = entry.ColorStyle.Titlebar;

  const bool entry_hovered = ctx.HoveredEntryIdx == entry_idx;

  if (GImGrid->SelectedEntryIndices.contains(entry_idx)) {
    entry_background = entry.ColorStyle.BackgroundSelected;
    titlebar_background = entry.ColorStyle.TitlebarSelected;
  } else if (entry_hovered) {
    entry_background = entry.ColorStyle.BackgroundHovered;
    titlebar_background = entry.ColorStyle.TitlebarHovered;
  }

  GImGrid->CanvasDrawList->AddRectFilled(entry.Rect.Min, entry.Rect.Max,
                                         entry_background,
                                         entry.LayoutStyle.CornerRounding);

  if (entry.TitleBarContentRect.GetHeight() > 0.f) {
    ImRect title_bar_rect = GetEntryTitleRect(entry);

    GImGrid->CanvasDrawList->AddRectFilled(
        title_bar_rect.Min, title_bar_rect.Max, titlebar_background,
        entry.LayoutStyle.CornerRounding, ImDrawFlags_RoundCornersTop);
  }

  ctx.CanvasDrawList->AddRect(
      entry.Rect.Min, entry.Rect.Max, entry.ColorStyle.Outline,
      entry.LayoutStyle.CornerRounding, ImDrawFlags_RoundCornersAll,
      entry.LayoutStyle.BorderThickness);

  if (entry_hovered)
    ctx.HoveredEntryIdx = entry_idx;

  DrawEntryDecorations(ctx, entry);
}

void BeginEntrySelection(const int entry_idx) {
  // Don't start selecting a node if we are e.g. already creating and dragging
  // a new link! New link creation can happen when the mouse is clicked over
  // a node, but within the hover radius of a pin.
  if (GImGrid->ClickInteraction.Type != ImGridClickInteractionType_None)
    return;

  // Handle resizing
  const ImGridEntryData &entry = GImGrid->Entries.Pool[entry_idx];
  if (entry.PreviewHeld)
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_Resizing;

  if (entry.PreviewHovered || entry.PreviewHeld)
    return;

  GImGrid->ClickInteraction.Type = ImGridClickInteractionType_Entry;
  // If the node is not already contained in the selection, then we want only
  // the interaction node to be selected, effective immediately.
  //
  // If the multiple selection modifier is active, we want to add this node
  // to the current list of selected nodes.
  //
  // Otherwise, we want to allow for the possibility of multiple nodes to be
  // moved at once.
  if (!GImGrid->SelectedEntryIndices.contains(entry_idx)) {
    if (!GImGrid->MultipleSelectModifier)
      GImGrid->SelectedEntryIndices.clear();
    GImGrid->SelectedEntryIndices.push_back(entry_idx);
  }
  // Deselect a previously-selected node
  else if (GImGrid->MultipleSelectModifier) {
    const int *const node_ptr = GImGrid->SelectedEntryIndices.find(entry_idx);
    GImGrid->SelectedEntryIndices.erase(node_ptr);

    // Don't allow dragging after deselecting
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_None;
  }

  // To support snapping of multiple nodes, we need to store the offset of
  // each node in the selection to the origin of the dragged node.
  const ImVec2 ref_origin = GImGrid->Entries.Pool[entry_idx].Origin;
  GImGrid->PrimaryEntryOffset = ref_origin + GImGrid->CanvasOriginScreenSpace +
                                GImGrid->Panning - GImGrid->MousePos;

  GImGrid->SelectedEntryOffsets.clear();
  for (int idx = 0; idx < GImGrid->SelectedEntryIndices.Size; idx++) {
    const int node = GImGrid->SelectedEntryIndices[idx];
    const ImVec2 node_origin = GImGrid->Entries.Pool[node].Origin - ref_origin;
    GImGrid->SelectedEntryOffsets.push_back(node_origin);
  }
}

ImOptionalIndex ResolveHoveredEntry(const ImVector<int> &depth_stack,
                                    const ImVector<int> OverlappingIndices) {
  if (OverlappingIndices.size() == 0) {
    return ImOptionalIndex();
  }

  if (OverlappingIndices.size() == 1) {
    return ImOptionalIndex(OverlappingIndices[0]);
  }

  int largest_depth_idx = -1;
  int node_idx_on_top = -1;

  for (int i = 0; i < OverlappingIndices.size(); ++i) {
    const int node_idx = OverlappingIndices[i];
    for (int depth_idx = 0; depth_idx < depth_stack.size(); ++depth_idx) {
      if (depth_stack[depth_idx] == node_idx &&
          (depth_idx > largest_depth_idx)) {
        largest_depth_idx = depth_idx;
        node_idx_on_top = node_idx;
      }
    }
  }

  IM_ASSERT(node_idx_on_top != -1);
  return ImOptionalIndex(node_idx_on_top);
}

void DrawGrid(const ImVec2 &canvas_size) {
  const ImVec2 offset = GImGrid->Panning;
  ImU32 line_color = GImGrid->Style.Colors[ImGridCol_GridLine];
  ImU32 line_color_prim = GImGrid->Style.Colors[ImGridCol_GridLinePrimary];
  bool draw_primary = GImGrid->Style.Flags & ImGridStyleFlags_GridLinesPrimary;

  for (float x = fmodf(offset.x, GImGrid->Style.GridSpacing); x < canvas_size.x;
       x += GImGrid->Style.GridSpacing) {
    GImGrid->CanvasDrawList->AddLine(
        SpaceToScreenSpace(ImVec2(x, 0.0f)),
        SpaceToScreenSpace(ImVec2(x, canvas_size.y)),
        offset.x - x == 0.f && draw_primary ? line_color_prim : line_color);
  }

  for (float y = fmodf(offset.y, GImGrid->Style.GridSpacing); y < canvas_size.y;
       y += GImGrid->Style.GridSpacing) {
    GImGrid->CanvasDrawList->AddLine(
        SpaceToScreenSpace(ImVec2(0.0f, y)),
        SpaceToScreenSpace(ImVec2(canvas_size.x, y)),
        offset.y - y == 0.f && draw_primary ? line_color_prim : line_color);
  }

  // add any previews
  for (int entry_idx = 0; entry_idx < GImGrid->Entries.Pool.size();
       ++entry_idx) {
    const auto &entry = GImGrid->Entries.Pool[entry_idx];
    bool inuse = GImGrid->Entries.InUse[entry_idx];
    if (!entry.HasPreview)
      continue;
    DrawEntryPreview(*GImGrid, entry);
  }
}

} // namespace

} // namespace ImGrid

namespace ImGrid {

ImGridContext *CreateContext() {
  ImGridContext *ctx = IM_NEW(ImGridContext)();
  if (GImGrid == NULL)
    SetCurrentContext(ctx);
  Initialize(ctx);
  return ctx;
}

ImGridContext *GetCurrentContext() { return GImGrid; }

void SetCurrentContext(ImGridContext *ctx) { GImGrid = ctx; }

ImGridIO &GetIO() { return GImGrid->IO; }

void StyleColorsDark(ImGridStyle *dest) {
  if (dest == nullptr)
    dest = &GImGrid->Style;

  dest->Colors[ImGridCol_EntryBackground] = IM_COL32(50, 50, 50, 255);
  dest->Colors[ImGridCol_EntryBackgroundHovered] = IM_COL32(75, 75, 75, 255);
  dest->Colors[ImGridCol_EntryBackgroundSelected] = IM_COL32(75, 75, 75, 255);
  dest->Colors[ImGridCol_EntryOutline] = IM_COL32(100, 100, 100, 255);
  dest->Colors[ImGridCol_EntryPreviewFill] = IM_COL32(0, 0, 225, 100);
  dest->Colors[ImGridCol_EntryPreviewOutline] = IM_COL32(0, 0, 175, 175);
  // title bar colors match ImGui's titlebg colors
  dest->Colors[ImGridCol_TitleBar] = IM_COL32(41, 74, 122, 255);
  dest->Colors[ImGridCol_TitleBarHovered] = IM_COL32(66, 150, 250, 255);
  dest->Colors[ImGridCol_TitleBarSelected] = IM_COL32(66, 150, 250, 255);
  dest->Colors[ImGridCol_BoxSelector] = IM_COL32(61, 133, 224, 30);
  dest->Colors[ImGridCol_BoxSelectorOutline] = IM_COL32(61, 133, 224, 150);

  dest->Colors[ImGridCol_GridBackground] = IM_COL32(40, 40, 50, 200);
  dest->Colors[ImGridCol_GridLine] = IM_COL32(200, 200, 200, 40);
  dest->Colors[ImGridCol_GridLinePrimary] = IM_COL32(240, 240, 240, 60);
}

void BeginGrid() {

  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_None);
  GImGrid->CurrentScope = ImGridScope_Grid;

  // reset state
  GImGrid->GridContentBounds = ImRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
  ObjectPoolReset(GImGrid->Entries);

  GImGrid->HoveredEntryIdx.Reset();
  GImGrid->HoveredEntryTitleBarIdx.Reset();
  GImGrid->EntryIndicesOverlappingWithMouse.clear();
  GImGrid->EntryTitleBarIndicesOverlappingWithMouse.clear();

  GImGrid->MousePos = ImGui::GetIO().MousePos;
  GImGrid->LeftMouseClicked = ImGui::IsMouseClicked(0);
  GImGrid->LeftMouseReleased = ImGui::IsMouseReleased(0);
  GImGrid->LeftMouseDragging = ImGui::IsMouseDragging(0, 0.0f);

  GImGrid->MultipleSelectModifier =
      (GImGrid->IO.MultipleSelectModifier.Modifier != NULL
           ? *GImGrid->IO.MultipleSelectModifier.Modifier
           : ImGui::GetIO().KeyCtrl);

  ImGui::BeginGroup();
  {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1.f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(40, 40, 50, 200));
    ImGui::BeginChild("editor_scrolling_region", ImVec2(0.f, 0.f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollWithMouse);
    GImGrid->CanvasOriginScreenSpace = ImGui::GetCursorScreenPos();

    // NOTE: we have to fetch the canvas draw list *after* we call
    // BeginChild(), otherwise the ImGui UI elements are going to be
    // rendered into the parent window draw list.
    DrawListSet(ImGui::GetWindowDrawList());

    {
      const ImVec2 canvas_size = ImGui::GetWindowSize();
      GImGrid->CanvasRectScreenSpace =
          ImRect(SpaceToScreenSpace(ImVec2(0.f, 0.f)),
                 SpaceToScreenSpace(canvas_size));

      DrawGrid(canvas_size);
    }
  }
}

void EndGrid() {
  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Grid);
  GImGrid->CurrentScope = ImGridScope_None;

  bool no_grid_content = GImGrid->GridContentBounds.IsInverted();
  if (no_grid_content)
    GImGrid->GridContentBounds =
        ScreenSpaceToGridSpace(*GImGrid, GImGrid->CanvasRectScreenSpace);

  if (GImGrid->LeftMouseClicked && ImGui::IsAnyItemActive())
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_ImGuiItem;

  if (GImGrid->ClickInteraction.Type == ImGridClickInteractionType_None &&
      MouseInCanvas()) {
    GImGrid->HoveredEntryIdx = ResolveHoveredEntry(
        GImGrid->EntryDepthOrder, GImGrid->EntryIndicesOverlappingWithMouse);
    GImGrid->HoveredEntryTitleBarIdx =
        ResolveHoveredEntry(GImGrid->EntryDepthOrder,
                            GImGrid->EntryTitleBarIndicesOverlappingWithMouse);
  }

  for (int entry_idx = 0; entry_idx < GImGrid->Entries.Pool.size();
       ++entry_idx) {
    if (GImGrid->Entries.InUse[entry_idx]) {
      DrawListActivateEntryBackground(entry_idx);
      DrawEntry(*GImGrid, entry_idx);
    }
  }

  GImGrid->CanvasDrawList->ChannelsSetCurrent(0);

  DrawListAppendClickInteractionChannel();
  DrawListActivateClickInteractionChannel();

  if (GImGrid->LeftMouseClicked) {
    // if (GImGrid->HoveredEntryIdx.HasValue())
    //   BeginEntrySelection(GImGrid->HoveredEntryIdx.Value());
    if (GImGrid->HoveredEntryTitleBarIdx.HasValue())
      BeginEntrySelection(GImGrid->HoveredEntryTitleBarIdx.Value());

  } else if (GImGrid->LeftMouseClicked || GImGrid->LeftMouseReleased ||
             GImGrid->AltMouseClicked || GImGrid->AltMouseScrollDelta != 0.f) {
    BeginCanvasInteraction();
  }

  ClickInteractionUpdate();

  ObjectPoolUpdate(GImGrid->Entries);

  DrawListSortChannelsByDepth(GImGrid->EntryDepthOrder);

  GImGrid->CanvasDrawList->ChannelsMerge();

  // pop style
  ImGui::EndChild();      // end scrolling region
  ImGui::PopStyleColor(); // pop child window background color
  ImGui::PopStyleVar();   // pop window padding
  ImGui::PopStyleVar();   // pop frame padding
  ImGui::EndGroup();
}

void BeginEntryTitleBar() {
  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Entry);
  ImGui::BeginGroup();
}

void EndEntryTitleBar() {
  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Entry);
  ImGui::EndGroup();

  ImGridEntryData &entry = GImGrid->Entries.Pool[GImGrid->CurrentEntryIdx];
  entry.TitleBarContentRect = GetItemRect();

  ImGui::ItemAdd(GetEntryTitleRect(entry), ImGui::GetID("title_bar"));

  ImGui::SetCursorPos(GridSpaceToSpace(*GImGrid, GetEntryContentOrigin(entry)));
}

void BeginEntry(const int entry_id) {
  // Must call BeginGrid() before BeginEntry()
  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Grid);
  GImGrid->CurrentScope = ImGridScope_Entry;

  const int entry_idx = ObjectPoolFindOrCreateIndex(GImGrid->Entries, entry_id);
  GImGrid->CurrentEntryIdx = entry_idx;

  ImGridEntryData &entry = GImGrid->Entries.Pool[entry_idx];
  entry.ColorStyle.Background =
      GImGrid->Style.Colors[ImGridCol_EntryBackground];
  entry.ColorStyle.BackgroundHovered =
      GImGrid->Style.Colors[ImGridCol_EntryBackgroundHovered];
  entry.ColorStyle.BackgroundSelected =
      GImGrid->Style.Colors[ImGridCol_EntryBackgroundSelected];
  entry.ColorStyle.Outline = GImGrid->Style.Colors[ImGridCol_EntryOutline];
  entry.ColorStyle.Titlebar = GImGrid->Style.Colors[ImGridCol_TitleBar];
  entry.ColorStyle.TitlebarHovered =
      GImGrid->Style.Colors[ImGridCol_TitleBarHovered];
  entry.ColorStyle.TitlebarSelected =
      GImGrid->Style.Colors[ImGridCol_TitleBarSelected];
  entry.ColorStyle.PreviewFill =
      GImGrid->Style.Colors[ImGridCol_EntryPreviewFill];
  entry.ColorStyle.PreviewOutline =
      GImGrid->Style.Colors[ImGridCol_EntryPreviewOutline];

  entry.LayoutStyle.CornerRounding = GImGrid->Style.EntryCornerRounding;
  entry.LayoutStyle.Padding = GImGrid->Style.EntryPadding;
  entry.LayoutStyle.BorderThickness = GImGrid->Style.EntryBorderThickness;

  ImGui::SetCursorPos(
      GridSpaceToSpace(*GImGrid, GetEntryTitleBarOrigin(entry)));

  DrawListAddEntry(entry_idx);
  DrawListActivateCurrentEntryForeground();

  ImGui::PushID(entry.Id);
  ImGui::BeginGroup();
}

void EndEntry() {

  IM_ASSERT(GImGrid->CurrentScope == ImGridScope_Entry);
  GImGrid->CurrentScope = ImGridScope_Grid;

  ImGui::EndGroup();
  ImGui::PopID();

  ImGridEntryData &entry = GImGrid->Entries.Pool[GImGrid->CurrentEntryIdx];
  entry.Rect = GetItemRect();
  entry.Rect.Expand(entry.LayoutStyle.Padding);

  GImGrid->GridContentBounds.Add(entry.Origin);
  GImGrid->GridContentBounds.Add(entry.Origin + entry.Rect.GetSize());

  if (entry.Rect.Contains(GImGrid->MousePos))
    GImGrid->EntryIndicesOverlappingWithMouse.push_back(
        GImGrid->CurrentEntryIdx);

  // GetEntryTitleRect adds padding and makes it full width
  if (GetEntryTitleRect(entry).Contains(GImGrid->MousePos)) {
    GImGrid->EntryTitleBarIndicesOverlappingWithMouse.push_back(
        GImGrid->CurrentEntryIdx);
  }
}

void RenderDebug() {

  ImGui::Text("Panning: %f %f", GImGrid->Panning.x, GImGrid->Panning.y);
  ImGui::Text("ContentRect: %f %f %f %f", GImGrid->GridContentBounds.Min.x,
              GImGrid->GridContentBounds.Min.y,
              GImGrid->GridContentBounds.Max.x,
              GImGrid->GridContentBounds.Max.y);

  ImGui::Text("Click Interaction: %d", GImGrid->ClickInteraction.Type);
  if (GImGrid->HoveredEntryIdx.HasValue())
    ImGui::Text("Hovered ID: %d", GImGrid->HoveredEntryIdx.Value());
  else
    ImGui::Text("Hovered ID: NA");

  if (GImGrid->HoveredEntryTitleBarIdx.HasValue())
    ImGui::Text("Hovered TB ID: %d", GImGrid->HoveredEntryTitleBarIdx.Value());
  else
    ImGui::Text("Hovered TB ID: NA");

  ImGui::Text("Mouse Pos: %f %f", GImGrid->MousePos.x, GImGrid->MousePos.y);

  for (int entry_idx = 0; entry_idx < GImGrid->Entries.Pool.size();
       ++entry_idx) {
    const auto &entry = GImGrid->Entries.Pool[entry_idx];
    bool inuse = GImGrid->Entries.InUse[entry_idx];
    ImGui::Text("%d: ", entry.Id);
    ImGui::Text("   %f %f", entry.Origin.x, entry.Origin.y);
    ImGui::Text("  C:  %f %f %f %f", entry.Rect.Min.x, entry.Rect.Min.y,
                entry.Rect.Max.x, entry.Rect.Max.y);
    ImGui::Text("  TB: %f %f %f %f", entry.TitleBarContentRect.Min.x,
                entry.TitleBarContentRect.Min.y,
                entry.TitleBarContentRect.Max.x,
                entry.TitleBarContentRect.Max.y);
    ImGui::Text("   Draggable: %d Resizable: %d", entry.Draggable,
                entry.Resizable);
  }
}

} // namespace ImGrid
