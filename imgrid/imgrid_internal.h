#pragma once

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_internal.h>

#include "imgrid.h"

#include <limits.h>
#include <map>

#define IM_MIN(x, y) x > y ? y : x
#define IM_MAX(x, y) x > y ? x : y

struct ImGridContext;

extern ImGridContext *GImGrid;

typedef int ImGridScope;
typedef int ImGridClickInteractionType;

enum ImGridScope_ {
  ImGridScope_None = 1,
  ImGridScope_Grid = 1 << 1,
  ImGridScope_Entry = 1 << 2,
};

enum ImGridClickInteractionType_ {
  ImGridClickInteractionType_None = 1,
  ImGridClickInteractionType_Entry = 1 << 1,
  ImGridClickInteractionType_ImGuiItem = 1 << 2,
  ImGridClickInteractionType_Resizing = 1 << 3,
};

// [SECTION] internal data structures
// from ImNodes

// The object T must have the following interface:
//
// struct T
// {
//     T();
//
//     int id;
// };
template <typename T> struct ImObjectPool {
  ImVector<T> Pool;
  ImVector<bool> InUse;
  ImVector<int> FreeList;
  ImGuiStorage IdMap;

  ImObjectPool() : Pool(), InUse(), FreeList(), IdMap() {}
};

// Emulates std::optional<int> using the sentinel value `INVALID_INDEX`.
struct ImOptionalIndex {
  ImOptionalIndex() : _Index(INVALID_INDEX) {}
  ImOptionalIndex(const int value) : _Index(value) {}

  // Observers

  inline bool HasValue() const { return _Index != INVALID_INDEX; }

  inline int Value() const {
    IM_ASSERT(HasValue());
    return _Index;
  }

  // Modifiers

  inline ImOptionalIndex &operator=(const int value) {
    _Index = value;
    return *this;
  }

  inline void Reset() { _Index = INVALID_INDEX; }

  inline bool operator==(const ImOptionalIndex &rhs) const {
    return _Index == rhs._Index;
  }

  inline bool operator==(const int rhs) const { return _Index == rhs; }

  inline bool operator!=(const ImOptionalIndex &rhs) const {
    return _Index != rhs._Index;
  }

  inline bool operator!=(const int rhs) const { return _Index != rhs; }

  static const int INVALID_INDEX = -1;

private:
  int _Index;
};

struct ImGridClickInteractionState {

  ImGridClickInteractionType Type;

  ImGridClickInteractionState() : Type(ImGridClickInteractionType_None) {}
};

struct ImGridPosition {
  float x, y, w, h;

  void Reset() { x = y = w = h = -1; };
  void SetDefault(const ImGridPosition &defaults) {
    if (x == -1)
      x = defaults.x;
    if (y == -1)
      y = defaults.y;
    if (w == -1)
      w = defaults.w;
    if (h == -1)
      h = defaults.h;
  }

  ImGridPosition() : x(-1), y(-1), w(-1), h(-1) {}
  ImGridPosition(float _x, float _y, float _w, float _h)
      : x(_x), y(_y), w(_w), h(_h) {}

  operator bool() const { return (x != -1 && y != -1 && w != -1 && h != -1); }
};

struct ImGridEntryData;

struct ImGridEntryInternal {
  ImGridPosition Position;
  ImGridEntryData *Parent;

  bool AutoPosition;
  float MinW, MinH;
  float MaxW, MaxH;
  bool NoResize;
  bool NoMove;
  bool Locked;

  bool Dirty;
  bool Updating;
  bool Moving;
  bool SkipDown;
  ImGridPosition PrevPosition;
  ImGridPosition Rect;
  ImVec2 LastUIPosition;
  ImGridPosition LastTried;
  ImGridPosition WillFitPos;

  ImGridEntryInternal(ImGridPosition pos, ImGridEntryData *parent)
      : Position(pos), Parent(parent) {}

  ImGridEntryInternal(ImGridEntryData *parent) : Position(), Parent(parent) {}
};

// return a && b && a.x === b.x && a.y === b.y && (a.w || 1) === (b.w || 1) &&
// (a.h || 1) === (b.h || 1);
inline bool operator==(const ImGridPosition &lhs, const ImGridPosition &rhs) {
  return (lhs.x == rhs.x) && (lhs.y == rhs.y) &&
         ((lhs.w != -1 ? lhs.w : 1) == (rhs.w != -1 ? rhs.w : 1)) &&
         ((lhs.h != -1 ? lhs.h : 1) == (rhs.h != -1 ? rhs.h : 1));
}

struct ImGridMoveOptions {
  ImGridPosition Position;
  float MinW, MinH;
  float MaxW, MaxH;

  ImGridEntryInternal *Skip;
  bool Pack;
  bool Nested;

  int CellWidth;
  int CellHeight;

  int MarginTop;
  int MarginBottom;
  int MarginLeft;
  int MarginRight;

  ImGridPosition Rect;

  bool Resizing;

  ImGridEntryInternal *Collide;

  bool ForceCollide;

  ImGridMoveOptions() {}
};

struct ImGridEntryData {
  int Id;
  ImVec2 Origin;
  ImRect Rect;
  ImRect TitleBarContentRect;

  ImGridEntryInternal GridData;

  bool Draggable;
  bool Resizable;
  bool Locked;
  bool Moving;

  ImRect PreviewRect;
  bool HasPreview;
  bool PreviewHeld;
  bool PreviewHovered;

  struct {
    ImU32 Background, BackgroundHovered, BackgroundSelected, Outline, Titlebar,
        TitlebarHovered, TitlebarSelected, PreviewFill, PreviewOutline;
  } ColorStyle;

  struct {
    float CornerRounding;
    ImVec2 Padding;
    float BorderThickness;
  } LayoutStyle;

  ImGridEntryData(const int id)
      : Id(id), Origin(0, 0), Rect(), TitleBarContentRect(), Draggable(true),
        Resizable(true), ColorStyle(), LayoutStyle(), HasPreview(false),
        PreviewHeld(false), PreviewHovered(false), GridData(this) {}
  ~ImGridEntryData() { Id = INT_MIN; }
};

struct ImGridInternal {
  int MaxRow;
  int Column;
  bool Float;
  bool PrevFloat;
  bool BatchMode;
  bool InColumnResize;
  bool HasLocked;
  bool Loading;
  ImVector<ImGridEntryInternal *> AddedEntries;
  ImVector<ImGridEntryInternal *> RemovedEntries;
  ImVector<ImGridEntryInternal *> Entries;
  std::map<int, ImVector<ImGridEntryInternal>> CacheLayouts;

  ImGridInternal(int column, int max_row,
                 ImVector<ImGridEntryInternal *> &nodes, bool _float)
      : Column(column), MaxRow(max_row), Entries(nodes), Float(_float) {}
  ImGridInternal(int column, int max_row)
      : Column(column), MaxRow(max_row), Entries(), Float(false) {}
  ImGridInternal() = default;
};

struct ImGridContext {
  ImObjectPool<ImGridEntryData> Entries;

  ImVec2 Panning;
  ImRect GridContentBounds;

  ImGridClickInteractionState ClickInteraction;

  ImDrawList *CanvasDrawList;

  ImVec2 CanvasOriginScreenSpace;
  ImRect CanvasRectScreenSpace;

  ImGuiStorage EntryIdxToSubmissionIdx;
  ImVector<int> EntryIdxSubmissionOrder;
  ImVector<int> EntryIndicesOverlappingWithMouse;
  ImVector<int> EntryTitleBarIndicesOverlappingWithMouse;

  ImVector<int> EntryDepthOrder;

  ImVector<int> SelectedEntryIndices;
  // Relative origins of selected nodes for snapping of dragged nodes
  ImVector<ImVec2> SelectedEntryOffsets;
  // Offset of the primary node origin relative to the mouse cursor.
  ImVec2 PrimaryEntryOffset;

  ImGridScope CurrentScope;

  ImGridIO IO;
  ImGridStyle Style;

  int CurrentEntryIdx;

  ImOptionalIndex HoveredEntryIdx;
  ImOptionalIndex HoveredEntryTitleBarIdx;

  ImVec2 MousePos;

  bool LeftMouseClicked;
  bool LeftMouseReleased;
  bool AltMouseClicked;
  bool LeftMouseDragging;
  bool AltMouseDragging;
  float AltMouseScrollDelta;
  bool MultipleSelectModifier;

  ImGridInternal *Grid;
};

namespace ImGrid {

static inline ImGridContext &Context() {
  // No Context was set! Did you forget to call ImGrid::CreateContext()?
  IM_ASSERT(GImGrid);
  return *GImGrid;
}

// [SECTION] ObjectPool implementation
// from ImNodes

template <typename T>
static inline int ObjectPoolFind(const ImObjectPool<T> &objects, const int id) {
  const int index = objects.IdMap.GetInt(static_cast<ImGuiID>(id), -1);
  return index;
}

template <typename T>
static inline void ObjectPoolUpdate(ImObjectPool<T> &objects) {
  for (int i = 0; i < objects.InUse.size(); ++i) {
    const int id = objects.Pool[i].Id;

    if (!objects.InUse[i] && objects.IdMap.GetInt(id, -1) == i) {
      objects.IdMap.SetInt(id, -1);
      objects.FreeList.push_back(i);
      (objects.Pool.Data + i)->~T();
    }
  }
}

template <> inline void ObjectPoolUpdate(ImObjectPool<ImGridEntryData> &nodes) {
  for (int i = 0; i < nodes.InUse.size(); ++i) {
    if (!nodes.InUse[i]) {
      const int id = nodes.Pool[i].Id;

      if (nodes.IdMap.GetInt(id, -1) == i) {
        // Remove node idx form depth stack the first time we detect that this
        // idx slot is unused
        ImVector<int> &depth_stack = GImGrid->EntryDepthOrder;
        const int *const elem = depth_stack.find(i);
        IM_ASSERT(elem != depth_stack.end());
        depth_stack.erase(elem);

        nodes.IdMap.SetInt(id, -1);
        nodes.FreeList.push_back(i);
        (nodes.Pool.Data + i)->~ImGridEntryData();
      }
    }
  }
}

template <typename T>
static inline void ObjectPoolReset(ImObjectPool<T> &objects) {
  if (!objects.InUse.empty()) {
    memset(objects.InUse.Data, 0, objects.InUse.size_in_bytes());
  }
}

template <typename T>
static inline int ObjectPoolFindOrCreateIndex(ImObjectPool<T> &objects,
                                              const int id) {
  int index = objects.IdMap.GetInt(static_cast<ImGuiID>(id), -1);

  // Construct new object
  if (index == -1) {
    if (objects.FreeList.empty()) {
      index = objects.Pool.size();
      IM_ASSERT(objects.Pool.size() == objects.InUse.size());
      const int new_size = objects.Pool.size() + 1;
      objects.Pool.resize(new_size);
      objects.InUse.resize(new_size);
    } else {
      index = objects.FreeList.back();
      objects.FreeList.pop_back();
    }
    IM_PLACEMENT_NEW(objects.Pool.Data + index) T(id);
    objects.IdMap.SetInt(static_cast<ImGuiID>(id), index);
  }

  // Flag it as used
  objects.InUse[index] = true;

  return index;
}

template <>
inline int ObjectPoolFindOrCreateIndex(ImObjectPool<ImGridEntryData> &nodes,
                                       const int node_id) {
  int node_idx = nodes.IdMap.GetInt(static_cast<ImGuiID>(node_id), -1);

  // Construct new node
  if (node_idx == -1) {
    if (nodes.FreeList.empty()) {
      node_idx = nodes.Pool.size();
      IM_ASSERT(nodes.Pool.size() == nodes.InUse.size());
      const int new_size = nodes.Pool.size() + 1;
      nodes.Pool.resize(new_size);
      nodes.InUse.resize(new_size);
    } else {
      node_idx = nodes.FreeList.back();
      nodes.FreeList.pop_back();
    }
    IM_PLACEMENT_NEW(nodes.Pool.Data + node_idx) ImGridEntryData(node_id);
    nodes.IdMap.SetInt(static_cast<ImGuiID>(node_id), node_idx);

    GImGrid->EntryDepthOrder.push_back(node_idx);
  }

  // Flag node as used
  nodes.InUse[node_idx] = true;

  return node_idx;
}

template <typename T>
static inline T &ObjectPoolFindOrCreateObject(ImObjectPool<T> &objects,
                                              const int id) {
  const int index = ObjectPoolFindOrCreateIndex(objects, id);
  return objects.Pool[index];
}

} // namespace ImGrid
