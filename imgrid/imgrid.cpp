
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

inline bool GridPositionsAreIntercepted(ImGridPosition a, ImGridPosition b) {
  return !(a.y >= b.y + b.h || a.y + a.h <= b.y || a.x + a.w <= b.x ||
           a.x >= b.x + b.w);
}

inline bool RectsAreTouching(ImGridEntryInternal &a, ImGridEntryInternal &b) {
  return GridPositionsAreIntercepted(a.Position,
                                     {b.Position.x - 0.5f, b.Position.y - 0.5f,
                                      b.Position.w + 1.f, b.Position.h + 1.f});
}

inline bool SwapEntryPositions(ImGridEntryInternal &a, ImGridEntryInternal &b) {
  if (a.Locked || b.Locked)
    return false;

  auto swapper = [&]() {
    auto x = b.Position.x;
    auto y = b.Position.y;
    b.Position.x = a.Position.x;
    b.Position.y = a.Position.y; // b -> a position
    if (a.Position.h != b.Position.h) {
      a.Position.x = x;
      a.Position.y = b.Position.y + b.Position.h; // a -> goes after b
    } else if (a.Position.w != b.Position.w) {
      a.Position.x = b.Position.x + b.Position.w;
      a.Position.y = y; // a -> goes after b
    } else {
      a.Position.x = x;
      a.Position.y = y; // a -> old b position
    }
    return true;
  };

  std::optional<bool> touching = false;
  // same size and same row or column, and touching
  if (a.Position.w == b.Position.w && a.Position.h == b.Position.h &&
      (a.Position.x == b.Position.x || a.Position.y == b.Position.y))
    if (RectsAreTouching(a, b))
      return swapper();
  if (touching.has_value() && !touching.value())
    return false; // IFF ran test and fail, bail out

  // check for taking same columns (but different height) and touching
  if (a.Position.w == b.Position.w && a.Position.x == b.Position.x &&
      (touching.value_or(false) || (RectsAreTouching(a, b)))) {
    return swapper();
  }
  if (!touching.value_or(false))
    return false;

  // check if taking same row (but different width) and touching
  if (a.Position.h == b.Position.h && a.Position.y == b.Position.y &&
      (touching || (RectsAreTouching(a, b)))) {
    return swapper();
  }
  return false;
}

bool GridFindEmptyPosition(ImGridEntryInternal &entry, int column,
                           ImVector<ImGridEntryInternal *> &entries,
                           ImGridEntryInternal *after) {
  int start = 0;
  if (after != NULL)
    start =
        after->Position.y * column + (after->Position.x + after->Position.w);

  bool found = false;
  for (int i = start; !found; ++i) {
    int x = i % column;
    int y = i / column;
    if (x + entry.Position.w > column)
      continue;
    ImGridPosition box = {static_cast<float>(x), static_cast<float>(y),
                          entry.Position.w, entry.Position.h};
    bool intercepted = false;
    for (int j = 0; j < entries.size(); ++j) {
      if (GridPositionsAreIntercepted(box, entries[j]->Position)) {
        intercepted = true;
        break;
      }
    }
    if (!intercepted) {
      if (entry.Position.x != x || entry.Position.y != y)
        entry.Dirty = true;

      entry.Position.x = x;
      entry.Position.y = y;
      found = true;
    }
  }
  return found;
}

int GridFindCacheLayout(ImGridInternal &ctx, ImGridEntryInternal *node,
                        int column) {
  int i = 0;
  for (const auto &cache_node : ctx.CacheLayouts[column]) {
    if (cache_node.Parent->Id == node->Parent->Id)
      return i;
    i++;
  }
  return -1;
}

void GridCacheOneLayout(ImGridInternal &ctx, ImGridEntryInternal *entry,
                        int column) {
  ImGridEntryInternal wrapped = {ImGridPosition{entry->Position.x,
                                                entry->Position.y,
                                                entry->Position.w, -1},
                                 entry->Parent};
  if (entry->AutoPosition || entry->Position.x == -1) {
    entry->Position.x = -1;
    entry->Position.y = -1;
    if (entry->AutoPosition)
      wrapped.AutoPosition = true;
  }

  int index = GridFindCacheLayout(ctx, entry, column);
  if (index < 0)
    ctx.CacheLayouts[column].push_back(wrapped);
  else
    ctx.CacheLayouts[column][index] = wrapped;
}

void GridNodeBoundFix(ImGridInternal &ctx, ImGridEntryInternal *entry,
                      bool resizing = false) {
  ImGridPosition pre = entry->PrevPosition;
  if (entry->PrevPosition.x == -1 || entry->PrevPosition.y == -1) {
    pre = entry->Position;
  }

  if (entry->MaxW >= 0)
    entry->Position.w = IM_MIN(entry->MaxW, entry->Position.w);
  if (entry->MaxH >= 0)
    entry->Position.h = IM_MIN(entry->MaxH, entry->Position.h);
  if (entry->MinW >= 0 && entry->MinW <= ctx.Column)
    entry->Position.w = IM_MAX(entry->MinW, entry->Position.w);
  if (entry->MinH >= 0)
    entry->Position.h = IM_MAX(entry->MinH, entry->Position.h);

  const bool save_orig = (entry->Position.x >= 0 ? entry->Position.x : 0) +
                             (entry->Position.w >= 0 ? entry->Position.w : 1) >
                         ctx.Column;
  if (save_orig && ctx.Column < 12 && !ctx.InColumnResize &&
      GridFindCacheLayout(ctx, entry, 12) == -1) {
    ImGridEntryInternal copy = *entry;
    if (copy.AutoPosition || copy.Position.x == -1) {
      copy.Position.x = -1;
      copy.Position.y = -1;
    } else {
      copy.Position.x = IM_MIN(copy.Position.x, 12 - 1);
    }
    copy.Position.w = IM_MIN(copy.Position.w != -1 ? copy.Position.w : 1, 12);
    GridCacheOneLayout(ctx, entry, 12);
  }

  if (entry->Position.w > ctx.Column)
    entry->Position.w = ctx.Column;
  else if (entry->Position.w < 1)
    entry->Position.w = 1;

  if (ctx.MaxRow > 0 && entry->Position.h > ctx.MaxRow)
    entry->Position.h = ctx.MaxRow;
  else if (entry->Position.h < 1)
    entry->Position.h = 1;

  entry->Position.x = IM_MAX(entry->Position.x, 0);
  entry->Position.y = IM_MAX(entry->Position.y, 0);

  if (entry->Position.x + entry->Position.w > ctx.Column) {
    if (resizing)
      entry->Position.w = ctx.Column - entry->Position.x;
    else
      entry->Position.x = ctx.Column - entry->Position.w;
  }

  if (ctx.MaxRow > 0 && entry->Position.y + entry->Position.h > ctx.MaxRow) {
    if (resizing)
      entry->Position.h = ctx.MaxRow - entry->Position.y;
    else
      entry->Position.y = ctx.MaxRow - entry->Position.h;
  }

  if (entry->Position == pre)
    entry->Dirty = true;
}

ImGridEntryInternal *GridPrepareEntry(ImGridInternal &ctx,
                                      ImGridEntryInternal *entry,
                                      bool resizing = false) {
  if (entry->Position.x == -1 || entry->Position.y == -1)
    entry->AutoPosition = true;

  ImGridPosition def = ImGridPosition{0, 0, 1, 1};
  entry->Position.SetDefault(def);

  GridNodeBoundFix(ctx, entry, resizing);
  return entry;
}

inline ImGridEntryInternal *GridCollide(ImGridInternal &ctx,
                                        ImGridEntryInternal *skip,
                                        ImGridPosition area,
                                        ImGridEntryInternal *skip2) {
  const auto skip_id = skip->Parent->Id;
  const auto skip2_id = skip2 == NULL ? -1 : skip2->Parent->Id;
  for (const auto &entry : ctx.Entries) {
    if (entry->Parent->Id != skip_id && entry->Parent->Id != skip2_id &&
        GridPositionsAreIntercepted(entry->Position, area))
      return entry;
  }
  return NULL;
}

inline ImVector<ImGridEntryInternal *>
GridCollideAll(ImGridInternal &ctx, ImGridEntryInternal *skip,
               ImGridPosition area, ImGridEntryInternal *skip2) {
  ImVector<ImGridEntryInternal *> collided;
  IM_ASSERT(skip != NULL);
  if (skip2 != NULL)
    IM_ASSERT(skip2->Parent != NULL);
  const auto skip_id = skip->Parent->Id;
  const auto skip2_id = skip2 == NULL ? -1 : skip2->Parent->Id;
  for (const auto &entry : ctx.Entries) {
    if (entry->Parent->Id != skip_id && entry->Parent->Id != skip2_id &&
        GridPositionsAreIntercepted(entry->Position, area))
      collided.push_back(entry);
  }
  return collided;
}

inline void GridSortNodesInplace(ImVector<ImGridEntryInternal *> &nodes,
                                 bool upwards) {
  int direction = upwards ? -1 : 1;
  int und = 10000;

  std::sort(
      nodes.begin(), nodes.end(),
      [&](ImGridEntryInternal *a, ImGridEntryInternal *b) {
        auto diffY = direction * ((a->Position.y == -1 ? und : a->Position.y) -
                                  (b->Position.y == -1 ? und : b->Position.y));
        if (diffY == 0)
          return direction * ((a->Position.x == -1 ? und : a->Position.x) -
                              (b->Position.x == -1 ? und : b->Position.x));
        return diffY;
      });
}

inline ImVector<ImGridEntryInternal *>
GridSortNodes(ImVector<ImGridEntryInternal *> nodes, bool upwards) {
  ImVector<ImGridEntryInternal *> sorted_nodes = nodes;
  GridSortNodesInplace(sorted_nodes, upwards);
  return sorted_nodes;
}

void GridPackEntries(ImGridInternal &ctx) {
  if (ctx.BatchMode)
    return;

  GridSortNodesInplace(ctx.Entries, true);

  if (ctx.Float) {
    for (auto &entry : ctx.Entries) {
      if (entry->Updating ||
          (entry->PrevPosition.x == -1 && entry->PrevPosition.y == -1 &&
           entry->PrevPosition.w == -1 && entry->PrevPosition.h == -1) ||
          entry->Position.y == entry->PrevPosition.y)
        continue;

      auto newY = entry->Position.y;
      while (newY > entry->PrevPosition.y) {
        --newY;
        auto *collided = GridCollide(
            ctx, entry,
            {entry->Position.x, newY, entry->Position.w, entry->Position.h},
            NULL);
        if (collided == NULL) {
          entry->Dirty = true;
          entry->Position.y = newY;
        }
      }
    }
  } else {
    // top grav pack
    int index = 0;
    for (auto &entry : ctx.Entries) {
      if (entry->Locked)
        continue;
      while (entry->Position.y > 0) {
        auto newY = (index == 0) ? 0 : entry->Position.y - 1;
        const bool can_be_moved =
            (index == 0) || GridCollide(ctx, entry,
                                        {entry->Position.x, newY,
                                         entry->Position.w, entry->Position.h},
                                        NULL) == NULL;
        if (!can_be_moved)
          break;
        entry->Dirty = (entry->Position.y != newY);
        entry->Position.y = newY;
      }
      index++;
    }
  }
}

inline ImGridEntryInternal *GridCopyPosition(ImGridEntryInternal *a,
                                             ImGridEntryInternal *b,
                                             bool include_minmax = false) {
  IM_ASSERT(a != NULL);
  // this allocates a new temporary "Node" that is used to perform
  // transformations

  if (b->Position.x != -1)
    a->Position.x = b->Position.x;
  if (b->Position.y != -1)
    a->Position.y = b->Position.y;
  if (b->Position.w != -1)
    a->Position.w = b->Position.w;
  if (b->Position.h != -1)
    a->Position.h = b->Position.h;

  if (include_minmax) {
    if (b->MinW != -1)
      a->MinW = b->MinW;
    if (b->MinH != -1)
      a->MinH = b->MinH;
    if (b->MaxW != -1)
      a->MaxW = b->MaxW;
    if (b->MaxH != -1)
      a->MaxH = b->MaxH;
  }
  return a;
}

inline ImGridEntryInternal *
GridCopyPositionFromOpts(ImGridEntryInternal *a, ImGridMoveOptions *b,
                         bool include_minmax = false) {
  IM_ASSERT(a != NULL);
  // this allocates a new temporary "Node" that is used to perform
  // transformations

  if (b->Position.x != -1)
    a->Position.x = b->Position.x;
  if (b->Position.y != -1)
    a->Position.y = b->Position.y;
  if (b->Position.w != -1)
    a->Position.w = b->Position.w;
  if (b->Position.h != -1)
    a->Position.h = b->Position.h;

  if (include_minmax) {
    if (b->MinW != -1)
      a->MinW = b->MinW;
    if (b->MinH != -1)
      a->MinH = b->MinH;
    if (b->MaxW != -1)
      a->MaxW = b->MaxW;
    if (b->MaxH != -1)
      a->MaxH = b->MaxH;
  }
  return a;
}

inline ImGridEntryInternal *
GridCopyPositionToOpts(ImGridEntryInternal *b, ImGridMoveOptions *a,
                       bool include_minmax = false) {
  IM_ASSERT(a != NULL);
  // this allocates a new temporary "Node" that is used to perform
  // transformations

  if (b->Position.x != -1)
    a->Position.x = b->Position.x;
  if (b->Position.y != -1)
    a->Position.y = b->Position.y;
  if (b->Position.w != -1)
    a->Position.w = b->Position.w;
  if (b->Position.h != -1)
    a->Position.h = b->Position.h;

  if (include_minmax) {
    if (b->MinW != -1)
      a->MinW = b->MinW;
    if (b->MinH != -1)
      a->MinH = b->MinH;
    if (b->MaxW != -1)
      a->MaxW = b->MaxW;
    if (b->MaxH != -1)
      a->MaxH = b->MaxH;
  }
  return b;
}

ImGridEntryInternal *
GridDirectionCollideCoverage(ImGridEntryInternal *entry,
                             ImGridMoveOptions &opts,
                             ImVector<ImGridEntryInternal *> &collides) {

  if (!entry->Rect || !opts.Rect)
    return NULL;

  ImGridPosition &r0 = entry->Rect;
  ImGridPosition &r = opts.Rect; // current dragged position
  if (r.y > r0.y) {
    r.h += r.y - r0.y;
    r.y = r0.y;
  } else {
    r.h += r0.y - r.y;
  }

  if (r.x > r0.x) {
    r.w += r.x - r0.x;
    r.x = r0.x;
  } else {
    r.w += r0.x - r.x;
  }

  ImGridEntryInternal *collide = NULL;
  float over_max = 0.5f;
  for (auto &n : collides) {
    if (n->Locked || !n->Rect) {
      break;
    }
    ImGridPosition &r2 = n->Rect; // overlapping target
    float yOver = 9999.9f, xOver = 9999.9f;
    // depending on which side we started from, compute the overlap % of
    // coverage (ex: from above/below we only compute the max horizontal line
    // coverage)
    if (r0.y < r2.y) { // from above
      yOver = ((r.y + r.h) - r2.y) / r2.h;
    } else if (r0.y + r0.h > r2.y + r2.h) { // from below
      yOver = ((r2.y + r2.h) - r.y) / r2.h;
    }
    if (r0.x < r2.x) { // from the left
      xOver = ((r.x + r.w) - r2.x) / r2.w;
    } else if (r0.x + r0.w > r2.x + r2.w) { // from the right
      xOver = ((r2.x + r2.w) - r.x) / r2.w;
    }
    float over = IM_MIN(xOver, yOver);
    if (over > over_max) {
      over_max = over;
      collide = n;
    }
  }

  opts.Collide = collide;
  return collide;
}

inline bool GridUseEntireRowArea(ImGridInternal &ctx,
                                 ImGridEntryInternal *entry,
                                 ImGridPosition new_position) {
  return (!ctx.Float || ctx.BatchMode && !ctx.PrevFloat) && !ctx.HasLocked &&
         (!entry->Moving || !entry->SkipDown ||
          new_position.y <= entry->Position.y);
}

bool GridFixCollisions(ImGridInternal &ctx, ImGridEntryInternal *entry,
                       ImGridPosition new_position, // = entry->Position,
                       ImGridEntryInternal *collide = NULL,
                       ImGridMoveOptions opts = {});

bool GridMoveNode(ImGridInternal &ctx, ImGridEntryInternal *entry,
                  ImGridMoveOptions &opts) {
  if (entry == NULL)
    return false;

  // might be wrong...
  bool was_undefined_pack;

  opts.Position.SetDefault(entry->Position);

  bool resizing = (entry->Position.w != opts.Position.w ||
                   entry->Position.h != opts.Position.h);
  ImGridEntryInternal new_node(entry->Parent);

  GridCopyPosition(&new_node, entry, true);
  new_node.Parent = entry->Parent;
  GridCopyPositionFromOpts(&new_node, &opts);
  GridNodeBoundFix(ctx, &new_node, resizing);
  GridCopyPositionToOpts(&new_node, &opts);

  if (!opts.ForceCollide && entry->Position == opts.Position)
    return false;

  ImGridPosition prev_pos = entry->Position;
  opts.Skip = NULL;

  ImVector<ImGridEntryInternal *> collided =
      GridCollideAll(ctx, entry, new_node.Position, opts.Skip);
  bool need_to_move = true;
  if (collided.size() > 0) {
    bool active_drag = entry->Moving && !opts.Nested;

    ImGridEntryInternal *collide =
        active_drag ? GridDirectionCollideCoverage(entry, opts, collided)
                    : collided[0];
    // if (active_drag && collide != NULL &&
    /* if (activeDrag && collide && node.grid?.opts?.subGridDynamic &&
  !node.grid._isTemp) { let over = Utils.areaIntercept(o.rect,
  collide._rect); let a1 = Utils.area(o.rect); let a2 =
  Utils.area(collide._rect); let perc = over / (a1 < a2 ? a1 : a2); if (perc
  > .8) { collide.grid.makeSubGrid(collide.el, undefined, node); collide =
  undefined;
    }
  }
  */
    if (collide != NULL) {
      need_to_move =
          !GridFixCollisions(ctx, entry, new_node.Position, collide, opts);
    } else {
      need_to_move = false;
    }
  }

  if (need_to_move) {
    entry->Dirty = true;
    GridCopyPosition(entry, &new_node);
  }

  if (opts.Pack) {
    GridPackEntries(ctx);
  }

  return entry->Position == prev_pos;
}

bool GridFixCollisions(ImGridInternal &ctx, ImGridEntryInternal *entry,
                       ImGridPosition new_position, // = entry->Position,
                       ImGridEntryInternal *collide, ImGridMoveOptions opts) {
  GridSortNodesInplace(ctx.Entries, false);

  collide =
      collide == NULL ? GridCollide(ctx, entry, new_position, NULL) : collide;
  if (collide == NULL)
    return false;

  if (entry->Moving && !opts.Nested && !ctx.Float) {
    if (SwapEntryPositions(*entry, *collide))
      return true;
  }

  ImGridPosition area = new_position;
  if (!ctx.Loading && GridUseEntireRowArea(ctx, entry, new_position)) {
    area = {0, new_position.y, static_cast<float>(ctx.Column), new_position.h};
    collide = GridCollide(ctx, entry, area, opts.Skip);
  }

  bool did_move = false;
  ImGridMoveOptions new_opts{};
  new_opts.Nested = true;
  new_opts.Pack = false;

  while (collide != NULL ||
         (collide = GridCollide(ctx, entry, area, opts.Skip))) {
    bool moved = false;

    if (collide->Locked || ctx.Loading ||
        entry->Moving && !entry->SkipDown &&
            new_position.y > entry->Position.y && !ctx.Float &&
            (GridCollide(ctx, collide,
                         {collide->Position.x, entry->Position.y,
                          collide->Position.w, collide->Position.h},
                         entry) == NULL ||
             GridCollide(ctx, collide,
                         {collide->Position.x,
                          new_position.y - collide->Position.h,
                          collide->Position.w, collide->Position.h},
                         entry) == NULL)) {
      entry->SkipDown = entry->SkipDown || new_position.y > entry->Position.y;
      ImGridMoveOptions opt = new_opts;
      printf("ASDFASDF\n");
      opt.Position = {new_position.x, collide->Position.y + collide->Position.h,
                      new_position.w, new_position.h};
      moved = GridMoveNode(ctx, entry, opt);
      if ((collide->Locked || ctx.Loading) && moved) {
        new_position = entry->Position;
      } else if (!collide->Locked && moved && opts.Pack) {
        GridPackEntries(ctx);
        new_position.y = collide->Position.y + collide->Position.h;
        entry->Position = new_position;
      }
      did_move = did_move || moved;
    } else {
      ImGridMoveOptions opt = new_opts;
      opt.Position = {collide->Position.x, new_position.y + new_position.h,
                      collide->Position.w, collide->Position.h};
      opt.Skip = entry;
      moved = GridMoveNode(ctx, collide, opt);
    }

    if (!moved)
      return did_move;

    collide = NULL;
  }
  return did_move;
}

ImGridEntryInternal *GridAddEntry(ImGridInternal &ctx,
                                  ImGridEntryInternal *entry,
                                  bool trigger_add_event = false,
                                  ImGridEntryInternal *after = NULL) {
  for (int i = 0; i < ctx.Entries.size(); i++) {
    if (ctx.Entries[i]->Parent->Id == entry->Parent->Id)
      return ctx.Entries[i];
  }

  ctx.InColumnResize ? (void)GridNodeBoundFix(ctx, entry)
                     : (void)GridPrepareEntry(ctx, entry);

  bool skip_collision = false;
  if (entry->AutoPosition &&
      GridFindEmptyPosition(*entry, ctx.Column, ctx.Entries, after)) {
    printf("auto position\n");
    entry->AutoPosition = false;
    skip_collision = true;
  }

  ctx.Entries.push_back(entry);
  if (trigger_add_event)
    ctx.AddedEntries.push_back(entry);

  if (!skip_collision)
    GridFixCollisions(ctx, entry, entry->Position);
  if (!ctx.BatchMode)
    GridPackEntries(ctx);
  return entry;
}

void GridRemoveEntry(ImGridInternal &ctx, ImGridEntryInternal *entry,
                     bool trigger_event = false) {
  bool found = false;
  for (int i = 0; i < ctx.Entries.size(); i++) {
    if (ctx.Entries[i]->Parent->Id == entry->Parent->Id)
      found = true;
  }

  if (!found)
    return;

  if (trigger_event)
    ctx.RemovedEntries.push_back(entry);

  // TODO:
  //  don't use 'faster' .splice(findIndex(),1) in case node isn't in our
  //  list, or in multiple times.
  // this.nodes = this.nodes.filter(n => n._id !== node._id);
  // if (!node._isAboutToRemove) this._packNodes(); // if dragged out, no
  // need to relayout as already done... this._notify([node]);
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

// TODO: does this need to be by ref?
bool GridChangedPosConstrain(ImGridEntryInternal *entry, ImGridPosition &p) {
  if (p.w == -1)
    p.w = entry->Position.w;
  if (p.h == -1)
    p.h = entry->Position.h;
  if (entry->Position.x != p.x || entry->Position.y != p.y)
    return true;

  // check constrained w,h
  if (entry->MaxW) {
    p.w = IM_MAX(p.w, entry->MaxW);
  }
  if (entry->MaxH) {
    p.h = IM_MAX(p.h, entry->MaxH);
  }
  if (entry->MinW) {
    p.w = IM_MAX(p.w, entry->MinW);
  }
  if (entry->MinH) {
    p.h = IM_MAX(p.h, entry->MinH);
  }
  return (entry->Position.w != p.w || entry->Position.h != p.h);
}

inline int GridGetRow(ImGridInternal &ctx) {
  int max_row = 0;
  for (auto &entry : ctx.Entries) {
    max_row = IM_MAX(max_row, entry->Position.y + entry->Position.h);
  }
  return max_row;
}

bool GridEntryMoveCheck(ImGridInternal &ctx, ImGridEntryInternal *entry,
                        ImGridMoveOptions opts) {
  if (!GridChangedPosConstrain(entry, opts.Position))
    return false;
  opts.Pack = true;

  if (!ctx.MaxRow)
    return GridMoveNode(ctx, entry, opts);

  ImGridEntryInternal *cloned_node = NULL;
  ImVector<ImGridEntryInternal *> cloned_nodes;
  for (auto &node : ctx.Entries) {
    if (node->Parent->Id == entry->Parent->Id)
      cloned_node = node;

    cloned_nodes.push_back(node);
  }
  ImGridInternal dev_grid =
      ImGridInternal{ctx.Column, 0, cloned_nodes, ctx.Float};
  if (cloned_node == NULL)
    return false;

  bool can_move = GridMoveNode(dev_grid, cloned_node, opts) &&
                  GridGetRow(dev_grid) <= IM_MAX(GridGetRow(ctx), ctx.MaxRow);
  if (!can_move && !opts.Resizing && opts.Collide != NULL) {
    // TODO: check
    if (SwapEntryPositions(*entry, *opts.Collide))
      return true;
  }
  if (!can_move)
    return false;

  for (auto &node : dev_grid.Entries) {
    if (node->Dirty) {
      for (auto &n : ctx.Entries) {
        if (n->Parent->Id == node->Parent->Id) {
          GridCopyPosition(n, node);
          n->Dirty = true;
        }
      }
    }
  }
  return true;
}

void GridCleanNodes(ImGridInternal &ctx) {
  if (ctx.BatchMode)
    return;

  for (auto &entry : ctx.Entries)
    entry->Dirty = false;
}

void GridSaveInitial(ImGridInternal &ctx) {
  for (auto &entry : ctx.Entries) {
    entry->PrevPosition = entry->Position;
    entry->Dirty = false;
    ctx.HasLocked |= entry->Locked;
  }
}

void GridBatchUpdate(ImGridInternal &ctx, bool flag = true,
                     bool do_pack = true) {
  if (ctx.BatchMode && !flag)
    return;

  ctx.BatchMode = flag;
  if (flag) {
    ctx.PrevFloat = ctx.Float;
    ctx.Float = true;
    GridCleanNodes(ctx);
    // GridSaveInitial();
  } else {
    ctx.Float = ctx.PrevFloat;
    if (do_pack)
      GridPackEntries(ctx);
  }
}

void GridCacheLayout(ImGridInternal &ctx, ImVector<ImGridEntryInternal *> nodes,
                     int column, bool clear = false) {
  ImVector<ImGridEntryInternal> entries;
  for (int i = 0; i < nodes.size(); ++i) {
    auto &node = nodes[i];
    // TODO: this is gross as we are only overwriting the h
    entries.push_back(
        ImGridEntryInternal{ImGridPosition{node->Position.x, node->Position.y,
                                           node->Position.w, -1},
                            node->Parent});
  }
  if (clear)
    ctx.Entries.clear();
  ctx.CacheLayouts[column] = entries;
}

void GridCompact(ImGridInternal &ctx,
                 ImGridColumnFlags opts = ImGridColumnFlags_Compact,
                 bool do_sort = true) {
  if (ctx.Entries.size() == 0)
    return;

  if (do_sort)
    GridSortNodesInplace(ctx.Entries, true);

  const auto was_batch = ctx.BatchMode;
  if (!was_batch)
    GridBatchUpdate(ctx);

  const auto was_column_resize = ctx.InColumnResize;
  if (was_column_resize)
    ctx.InColumnResize = true;

  ImVector<ImGridEntryInternal *> new_entries = ctx.Entries; // copy
  ctx.Entries.clear();

  for (int i = 0; i < new_entries.size(); ++i) {
    auto *n = new_entries[i];
    ImGridEntryInternal *after = NULL;

    if (!n->Locked) {
      n->AutoPosition = true;
      if (opts & ImGridColumnFlags_List && i > 0) {
        after = new_entries[i - 1];
      }
    }

    GridAddEntry(ctx, n, false, after);
  }

  if (!was_column_resize)
    ctx.InColumnResize = false;

  if (!was_batch)
    GridBatchUpdate(ctx, false, false);
}

void GridColumnChanged(ImGridInternal &ctx, int previous_column, int column,
                       ImGridColumnOptions opts = ImGridColumnOptions{
                           ImGridColumnFlags_MoveScale}) {
  if (ctx.Entries.size() == 0 || previous_column == column)
    return;

  if (opts.Flags == ImGridColumnFlags_None)
    return;

  bool compact = opts.Flags & ImGridColumnFlags_Compact ||
                 opts.Flags & ImGridColumnFlags_List;
  if (compact)
    GridSortNodesInplace(ctx.Entries, true);

  if (column < previous_column)
    GridCacheLayout(ctx, ctx.Entries, previous_column);
  GridBatchUpdate(ctx);

  ImVector<ImGridEntryInternal *> new_entries;
  ImVector<ImGridEntryInternal *> ordered_entries =
      compact ? ctx.Entries : GridSortNodes(ctx.Entries, false);
  if (column > previous_column) {
    int last_index = ctx.CacheLayouts.size() - 1;
    ImVector<ImGridEntryInternal> &cache_nodes = ctx.CacheLayouts[last_index];
    if (!(cache_nodes.size() > 0) && previous_column != last_index &&
        ctx.CacheLayouts[last_index].size() > 0) {
      previous_column = last_index;
      for (auto &entry_wrapper : cache_nodes) {
        // find the matching entry in ordered_entries
        ImGridEntryInternal *inner_entry = NULL;
        for (int node_ind = 0;
             node_ind < ordered_entries.size() && inner_entry == NULL;
             ++node_ind) {
          if (ordered_entries[node_ind]->Parent->Id == entry_wrapper.Parent->Id)
            inner_entry = ordered_entries[node_ind];
        }
        if (inner_entry != NULL) {
          if (!compact && !inner_entry->AutoPosition) {
            inner_entry->Position.x = entry_wrapper.Position.x;
            inner_entry->Position.y = entry_wrapper.Position.y;
          }
          inner_entry->Position.w = entry_wrapper.Position.w;
        }
      }
    }

    // new
    for (auto &cache_node : cache_nodes) {
      ImGridEntryInternal *inner_entry = NULL;
      int found_index = -1;
      for (int node_ind = 0;
           node_ind < ordered_entries.size() && inner_entry == NULL;
           ++node_ind) {
        if (ordered_entries[node_ind]->Parent->Id == cache_node.Parent->Id)
          inner_entry = ordered_entries[node_ind];
      }
      if (inner_entry != NULL) {
        if (compact) {
          inner_entry->Position.w = cache_node.Position.w;
          continue;
        }

        if (cache_node.AutoPosition || cache_node.Position.x == -1 ||
            cache_node.Position.y == -1) {
          GridFindEmptyPosition(cache_node, ctx.Column, new_entries, NULL);
        }
        if (!cache_node.AutoPosition) {
          inner_entry->Position.x = cache_node.Position.x;
          inner_entry->Position.y = cache_node.Position.y;
          inner_entry->Position.w = cache_node.Position.w;
          new_entries.push_back(inner_entry);
        }
        // remove found_index from ordered_entries
        ordered_entries.erase(ordered_entries.begin() + found_index);
      }
    }
  }

  if (compact) {
    GridCompact(ctx, opts.Flags);
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
          entry->Position.x =
              (column == 1 ? 0
                           : (move ? IM_ROUND(entry->Position.x * ratio)
                                   : IM_MIN(entry->Position.x, column - 1)));
          entry->Position.w = ((column == 1 || previous_column == 1) ? 1
                               : scale ? (IM_ROUND(entry->Position.w * ratio))
                                       : IM_MIN(entry->Position.w, column));
          new_entries.push_back(entry);
        }
        ordered_entries.clear();
      }
    }

    GridSortNodesInplace(new_entries, false);
    ctx.InColumnResize = true;
    ctx.Entries.clear();
    for (int i = 0; i < new_entries.size(); ++i) {
      GridAddEntry(ctx, new_entries[i], false);
      new_entries[i]->PrevPosition.Reset();
    }
  }

  for (int i = 0; i < ctx.Entries.size(); ++i) {
    ctx.Entries[i]->PrevPosition.Reset();
  }
  GridBatchUpdate(ctx, false, !compact);
  ctx.InColumnResize = false;
}

void GridBeginUpdate(ImGridInternal &ctx, ImGridEntryInternal *node) {
  if (!node->Updating) {
    node->Updating = true;
    node->SkipDown = false;
    if (!ctx.BatchMode)
      GridSaveInitial(ctx);
  }
}

void GridEndUpdate(ImGridInternal &ctx) {
  for (auto &entry : ctx.Entries) {
    if (entry->Updating) {
      entry->Updating = false;
      entry->SkipDown = false;
    }
  }
}

// SECTION[DrawLists]
// The draw list channels are structured as follows. First we have our base
// channel, the canvas grid on which we render the grid lines in
// BeginNodeEditor(). The base channel is the reason
// draw_list_submission_idx_to_background_channel_idx offsets the index by
// one. Each BeginEntry() call appends two new draw channels, for the entry
// background and foreground. The node foreground is the channel into which
// the node's ImGui content is rendered. Finally, in EndNodeEditor() we
// append one last draw channel for rendering the selection box and the
// incomplete link on top of everything else.
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

    // If we're inside the old capacity region of the array, we need to
    // reuse the existing memory of the command and index buffers.
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
  // NOTE: don't use this function outside of EndNodeEditor. Using this
  // before all nodes have been added will screw up the node draw order.
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
  // There is a discrepancy in the submitted node count and the rendered
  // node count! Did you call one of the following functions
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

  // TODO: this is an O(N^2) algorithm. It might be worthwhile revisiting
  // this to see if the time complexity can be reduced.

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

void GridTriggerChangeEvent(ImGridInternal &ctx) {
  if (ctx.BatchMode)
    return;

  GridSaveInitial(ctx);
}

void GridCacheRects(ImGridInternal &ctx, float w, float h, float top,
                    float right, float bottom, float left) {
  for (auto &entry : ctx.Entries) {
    entry->Rect = ImGridPosition{
        entry->Position.x * h + top,
        entry->Position.y * w + left,
        entry->Position.w * h + top,
        entry->Position.h * w + left,
    };
  };
}

// Public Grid API
void MoveNode(ImGridInternal &ctx, ImGridEntryInternal *entry,
              ImGridMoveOptions opts) {
  const bool was_updating = entry->Updating;
  if (!was_updating) {
    GridCleanNodes(ctx);
    GridBeginUpdate(ctx, entry);
  }
  GridMoveNode(ctx, entry, opts);
  if (!was_updating) {
    GridTriggerChangeEvent(ctx);
    GridEndUpdate(ctx);
  }
}

ImVec2 SnapOriginToGrid(ImVec2 origin) { return origin; }

void TranslateSelectedEntries() {
  // TODO: impl _dragOrResize
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

    if (GImGrid->Grid != NULL) {
      entry.GridData.LastUIPosition = GImGrid->MousePos;
      entry.GridData.Moving = true;
      ImGridMoveOptions opts{};
      opts.Position = {origin.x + entry_rel.x, origin.y + entry_rel.y,
                       entry.GridData.Position.w, entry.GridData.Position.h};
      opts.Skip = NULL;
      MoveNode(*GImGrid->Grid, &entry.GridData, opts);
      printf("Post Move Position %f %f %f %f\n", entry.GridData.Position.x,
             entry.GridData.Position.y, entry.GridData.Position.w,
             entry.GridData.Position.h);
    }
  }

  GridCacheRects(*GImGrid->Grid, 50, 50, 0, 0, 0, 0);

  // add a preview box where this will snap to if dropped
  for (int i = 0; i < GImGrid->SelectedEntryIndices.size(); ++i) {
    const ImVec2 entry_rel = GImGrid->SelectedEntryOffsets[i];
    const int entry_idx = GImGrid->SelectedEntryIndices[i];
    ImGridEntryData &entry = GImGrid->Entries.Pool[entry_idx];

    // have to go from grid space x, y, w, h to a rect of min and max x,y
    entry.PreviewRect = ImRect(
        ImVec2(entry.GridData.Position.x - entry.GridData.Position.w / 2,
               entry.GridData.Position.y - entry.GridData.Position.h / 2),
        ImVec2(entry.GridData.Position.x + entry.GridData.Position.w / 2,
               entry.GridData.Position.y + entry.GridData.Position.h / 2));

    entry.HasPreview = true;
  }
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
        entry.GridData.Moving = false;
        entry.Origin = {entry.GridData.Rect.x, entry.GridData.Rect.y};
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
  // Don't start selecting a node if we are e.g. already creating and
  // dragging a new link! New link creation can happen when the mouse is
  // clicked over a node, but within the hover radius of a pin.
  if (GImGrid->ClickInteraction.Type != ImGridClickInteractionType_None)
    return;

  // Handle resizing
  const ImGridEntryData &entry = GImGrid->Entries.Pool[entry_idx];
  if (entry.PreviewHeld)
    GImGrid->ClickInteraction.Type = ImGridClickInteractionType_Resizing;

  if (entry.PreviewHovered || entry.PreviewHeld)
    return;

  GImGrid->ClickInteraction.Type = ImGridClickInteractionType_Entry;
  // If the node is not already contained in the selection, then we want
  // only the interaction node to be selected, effective immediately.
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

  if (GImGrid->Grid == NULL)
    GImGrid->Grid = IM_NEW(ImGridInternal)(4, 4);

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

  // see if we have this node in our grid
  if (GImGrid->Grid != NULL) {
    bool found = false;
    for (auto &entry_ : GImGrid->Grid->Entries) {
      if (entry_->Parent->Id == GImGrid->CurrentEntryIdx) {
        found = true;
      }
    }

    entry.GridData.Position.w = ceil((entry.Rect.GetWidth()) / 50);
    entry.GridData.Position.h = ceil((entry.Rect.GetHeight()) / 50);

    if (!found) {
      GridAddEntry(*GImGrid->Grid, &entry.GridData, false);
    }
  }
}

void RenderDebug() {

  ImGui::Text("Panning: %f %f", GImGrid->Panning.x, GImGrid->Panning.y);

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

    ImGui::Text("Grid x: %f y: %f w: %f h: %f", entry.GridData.Position.x,
                entry.GridData.Position.y, entry.GridData.Position.w,
                entry.GridData.Position.h);
    ImGui::Text("Moving: %d", entry.GridData.Moving);
  }
}

} // namespace ImGrid
