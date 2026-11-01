#ifndef RUNEC_VIEWER_CONTEXT_MENU_H
#define RUNEC_VIEWER_CONTEXT_MENU_H

#define RUNEC_CONTEXT_MENU_HEADER_HEIGHT 19
#define RUNEC_CONTEXT_MENU_ROW_HEIGHT 15
#define RUNEC_CONTEXT_MENU_PADDING_X 4
#define RUNEC_CONTEXT_MENU_BOTTOM_PADDING 3
#define RUNEC_CONTEXT_MENU_DISMISS_MARGIN 10

typedef struct {
    int x;
    int y;
    int width;
    int height;
} RuneCContextMenuLayout;

RuneCContextMenuLayout runec_context_menu_layout(
    int anchor_x, int anchor_y, int screen_width, int screen_height,
    int content_width, int action_count);
int runec_context_menu_contains(const RuneCContextMenuLayout *layout,
                                int x, int y);
int runec_context_menu_contains_margin(const RuneCContextMenuLayout *layout,
                                       int x, int y, int margin);
int runec_context_menu_action_at(const RuneCContextMenuLayout *layout,
                                 int action_count, int x, int y);

#endif
