#pragma once

#include <cstddef>
#include <functional>
#include <imgui.h>
#include <stddef.h>

typedef int ImGridCol;         // -> enum ImGridCol_
typedef int ImGridStyleVar;    // -> enum ImGridStyleVar_
typedef int ImGridStyleFlags;  // -> enum ImGridStyleFlags_
typedef int ImGridColumnFlags; // -> emum ImGridColumnFlags_

enum ImGridCol_ {
  ImGridCol_EntryBackground = 0,
  ImGridCol_EntryBackgroundHovered,
  ImGridCol_EntryBackgroundSelected,
  ImGridCol_EntryOutline,
  ImGridCol_TitleBar,
  ImGridCol_TitleBarHovered,
  ImGridCol_TitleBarSelected,
  ImGridCol_BoxSelector,
  ImGridCol_BoxSelectorOutline,
  ImGridCol_GridBackground,
  ImGridCol_GridLine,
  ImGridCol_GridLinePrimary,
  ImGridCol_EntryPreviewOutline,
  ImGridCol_EntryPreviewFill,
  ImGridCol_COUNT
};

enum ImGridStyleVar_ {
  ImGridStyleVar_GridSpacing = 0,
  ImGridStyleVar_EntryCornerRounding,
  ImGridStyleVar_EntryPadding,
  ImGridStyleVar_EntryBorderThickness,
  ImGridStyleVar_COUNT
};

enum ImGridStyleFlags_ {
  ImGridStyleFlags_None = 0,
  ImGridStyleFlags_EntryOutline = 1 << 0,
  ImGridStyleFlags_GridLines = 1 << 2,
  ImGridStyleFlags_GridLinesPrimary = 1 << 3,
  ImGridStyleFlags_GridSnapping = 1 << 4
};

enum ImGridColumnFlags_ {
  ImGridColumnFlags_None = 0,
  ImGridColumnFlags_MoveScale = 1 << 0,
  ImGridColumnFlags_Compact = 1 << 1,
  ImGridColumnFlags_List = 1 << 2,
  ImGridColumnFlags_Scale = 1 << 3,
  ImGridColumnFlags_Move = 1 << 4,
};

struct ImGuiContext;
struct ImVec2;
struct ImRect;
struct ImGridEntryData;

struct ImGridColumnOptions {
  ImGridColumnFlags Flags;
  std::function<void(int, int, ImVector<ImGridEntryData *>,
                     ImVector<ImGridEntryData *>)>
      Func;
};

struct ImGridStyle {
  float GridSpacing;

  float EntryCornerRounding;
  ImVec2 EntryPadding;
  float EntryBorderThickness;

  // By default, ImNodesStyleFlags_NodeOutline and ImNodesStyleFlags_Gridlines
  // are enabled.
  ImGridStyleFlags Flags;
  // Set these mid-frame using Push/PopColorStyle. You can index this color
  // array with with a ImNodesCol value.
  unsigned int Colors[ImGridCol_COUNT];

  ImGridStyle();
};

struct ImGridContext;

struct ImGridIO {

  struct MultipleSelectModifier {
    MultipleSelectModifier();

    const bool *Modifier;
  } MultipleSelectModifier;
};

namespace ImGrid {

void SetImGuiContext(ImGuiContext *ctx);

ImGridContext *CreateContext();
void DestroyContext(
    ImGridContext *ctx = NULL); // NULL = destroy current context
ImGridContext *GetCurrentContext();
void SetCurrentContext(ImGridContext *ctx);

ImGridIO &GetIO();

void StyleColorsDark(ImGridStyle *dest);

// Returns the global style struct. See the struct declaration for default
// values.
ImGridStyle &GetStyle();
// Style presets matching the dear imgui styles of the same name. If dest is
// NULL, the active context's ImGridStyle instance will be used as the
// destination.
void StyleColorsDark(ImGridStyle *dest = NULL); // on by default
void StyleColorsClassic(ImGridStyle *dest = NULL);
void StyleColorsLight(ImGridStyle *dest = NULL);

// Use PushColorStyle and PopColorStyle to modify ImGridStyle::Colors
// mid-frame.
void PushColorStyle(ImGridCol item, unsigned int color);
void PopColorStyle();
void PushStyleVar(ImGridStyleVar style_item, float value);
void PushStyleVar(ImGridStyleVar style_item, const ImVec2 &value);
void PopStyleVar(int count = 1);

// Main functions
void BeginGrid();
void EndGrid();

void BeginEntry(const int id);
void EndEntry();

void BeginEntryTitleBar();
void EndEntryTitleBar();

// Helper functions

ImRect GetEntryRect();

bool IsGridHovered();

bool IsEntryHovered(int *entry_id);

void RenderDebug();

} // namespace ImGrid
