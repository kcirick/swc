#include <assert.h>
#include <signal.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
//
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
//
//#include <wlr/types/wlr_foreign_toplevel_management_v1.h>

#include "dwl-ipc-unstable-v2-protocol.h"
#include "globals.h"
#include "layer.h"
#include "client.h"
#include "server.h"
#include "input.h"
#include "ipc.h"

//--- client outline procedures ------------------------------------------
static void
client_outline_destroy_notify(struct wl_listener *listener, void *data)
{
   struct client_outline* outline = wl_container_of(listener, outline, destroy);
   wl_list_remove(&outline->destroy.link);
   free(outline);
}

struct client_outline*
client_outline_create(struct wlr_scene_tree *parent, float* border_colour, int line_width)
{
   struct client_outline* outline = calloc(1, sizeof(struct client_outline));
   outline->line_width = line_width;
   outline->tree = wlr_scene_tree_create(parent);

   outline->top = wlr_scene_rect_create(outline->tree, 0, 0, border_colour);
   outline->bottom = wlr_scene_rect_create(outline->tree, 0, 0, border_colour);
   outline->left = wlr_scene_rect_create(outline->tree, 0, 0, border_colour);
   outline->right = wlr_scene_rect_create(outline->tree, 0, 0, border_colour);

   LISTEN(&outline->tree->node.events.destroy, &outline->destroy, client_outline_destroy_notify);
   
   return outline;
}

void
client_outline_set_size(struct client_outline* outline, int width, int height) {
   //borders
   int bw = outline->line_width;
   //top
   wlr_scene_rect_set_size(outline->top, width, bw);
   wlr_scene_node_set_position(&outline->top->node, 0, -bw);
   //bottom
   wlr_scene_rect_set_size(outline->bottom, width, bw);
   wlr_scene_node_set_position(&outline->bottom->node, 0, height);
   //left
   wlr_scene_rect_set_size(outline->left, bw, height + 2 * bw);
   wlr_scene_node_set_position(&outline->left->node, -bw, -bw);
   //right
   wlr_scene_rect_set_size(outline->right, bw, height + 2 * bw);
   wlr_scene_node_set_position(&outline->right->node, width, -bw);
}

//------------------------------------------------------------------------
void
setCurrentTag(int tag, bool toggle)
{
   struct simple_output* output = g_server->cur_output;
   if(toggle)
      output->visible_tags ^= TAGMASK(tag);
   else 
      output->visible_tags = output->current_tag = TAGMASK(tag);

   focus_client(get_top_client_from_output(output, false), true);
   //arrange_output(output);
   print_server_info();
}

void
tileTag() 
{
   struct simple_client* client;
   struct simple_output* output = g_server->cur_output;
   
   // first count the number of clients
   int n=0;
   wl_list_for_each(client, &g_server->clients, link){
      if(!(client->visible && VISIBLEON(client, output))) continue;
      n++;
   }

   int gap_width = g_config->tile_gap_width;
   int bw = g_config->border_width;

   int i=0;
   struct wlr_box new_geom;
   wl_list_for_each(client, &g_server->clients, link){
      if(!(client->visible && VISIBLEON(client, output))) continue;
      
      if(i==0) { // master window
         new_geom.x = output->usable_area.x + gap_width + bw;
         new_geom.y = output->usable_area.y + gap_width + bw;
         new_geom.width = (output->usable_area.width - (gap_width*(MIN(2,n)+1)))/MIN(2,n) - bw*2;
         new_geom.height = output->usable_area.height - gap_width*2 - bw*2;
         client->geom = new_geom;

         set_client_geometry(client, false);
      } else {
         new_geom.x = output->usable_area.x + output->usable_area.width/2 + gap_width/2 + bw;
         new_geom.width = (output->usable_area.width - (gap_width*3))/2 - bw*2;
         new_geom.height = (output->usable_area.height - (gap_width*n))/(n-1);
         new_geom.y = output->usable_area.y + (gap_width*i) + (new_geom.height*(i-1)) + bw;
         new_geom.height -= 2*bw;
         client->geom = new_geom;
         
         set_client_geometry(client, false);
      }
      i++;
   }
   //arrange_output(output);
}

struct simple_output*
get_output_at(double x, double y)
{
   struct wlr_output *output = wlr_output_layout_output_at(g_server->output_layout, x, y);
   return output ? output->data : NULL;
}

void
print_server_info() 
{
   struct simple_output* output;
   struct simple_client* client;

   wl_list_for_each(output, &g_server->outputs, link) {
      ipc_output_printstatus(output);
      say(DEBUG, "output %s", output->wlr_output->name);
      say(DEBUG, " -> cur_output = %u", output == g_server->cur_output);
      say(DEBUG, " -> tag = vis:%u / cur:%u", output->visible_tags, output->current_tag);
      wl_list_for_each(client, &g_server->clients, link) {
         struct simple_client* focused_client=NULL;
         get_client_from_surface(g_server->seat->keyboard_state.focused_surface, &focused_client, NULL);
         say(DEBUG, " -> client");
         say(DEBUG, "    -> client focused = %b", client == focused_client); 
         say(DEBUG, "    -> client title = %s", get_client_title(client));
         say(DEBUG, "    -> client tag = %u", client->tag);
         say(DEBUG, "    -> client fixed = %b", client->fixed);
         say(DEBUG, "    -> client visible = %b", client->visible);
         say(DEBUG, "    -> client urgent = %b", client->urgent);
      }
   }
}

void
check_idle_inhibitor()
{
   int inhibited=0, lx, ly;
   struct wlr_idle_inhibitor_v1 *inhibitor;
   bool bypass_surface_visibility = 0; // 1 means idle inhibitors will disable idle tracking 
                                       // even if its surface isn't visible (from dwl)
   wl_list_for_each(inhibitor, &g_server->idle_inhibit_manager->inhibitors, link) {
      struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
      struct wlr_scene_tree *tree = surface->data;
      if(surface && (bypass_surface_visibility || 
               (!tree || wlr_scene_node_coords(&tree->node, &lx, &ly)) )) {
         inhibited = 1;
         break;
      }
   }
   wlr_idle_notifier_v1_set_inhibited(g_server->idle_notifier, inhibited);
}

void
arrange_output(struct simple_output* output)
{
   say(DEBUG, "arrange_output");
   struct simple_client* client, *focused_client=NULL;

   get_client_from_surface(g_server->seat->keyboard_state.focused_surface, &focused_client, NULL);
   
   int n=0;
   wl_list_for_each(client, &g_server->clients, link) {
      if(client->visible && VISIBLEON(client, output)) n++;
      set_client_border_colour(client, client==focused_client ? FOCUSED : UNFOCUSED);
      wlr_scene_node_set_enabled(&client->scene_tree->node, client->visible && VISIBLEON(client, output));
   }

   if(n>0){
      if(!focused_client)
         focused_client = get_top_client_from_output(output, false);
      focus_client(focused_client, true);
   } else
      input_focus_surface(NULL);

   check_idle_inhibitor();
}

//--- Other notify functions ---------------------------------------------
static void
new_decoration_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "new_decoration_notify");
   struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
   wlr_xdg_toplevel_decoration_v1_set_mode(decoration, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void
inhibitor_destroy_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "inhibitor_destroy_notify");
   wlr_idle_notifier_v1_set_inhibited(g_server->idle_notifier, 0);
}

static void
new_inhibitor_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "new_inhibitor_notify");
   struct wlr_idle_inhibitor_v1 *inhibitor = data;

   LISTEN(&inhibitor->events.destroy, &g_server->inhibitor_destroy, inhibitor_destroy_notify);
   check_idle_inhibitor();
}

static void
output_pm_set_mode_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "output_pm_set_mode_notify");
   struct wlr_output_power_v1_set_mode_event *event = data;

   switch (event->mode) {
      case ZWLR_OUTPUT_POWER_V1_MODE_OFF:
         wlr_output_enable(event->output, false);
         wlr_output_commit(event->output);
         break;
      case ZWLR_OUTPUT_POWER_V1_MODE_ON:
         wlr_output_enable(event->output, true);
         if(!wlr_output_test(event->output))
            wlr_output_rollback(event->output);
         wlr_output_commit(event->output);

         // reset the cursor image
         wlr_cursor_unset_image(g_server->cursor);
         wlr_cursor_set_xcursor(g_server->cursor, g_server->cursor_manager, "left_ptr");;
         break;
   }
}

static void
set_gamma_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "set_gamma_notify");
  
   struct wlr_gamma_control_manager_v1_set_gamma_event *event = data;
   struct simple_output *output = event->output->data;
   output->gamma_lut_changed = true;
   wlr_output_schedule_frame(output->wlr_output);
}

static void
urgent_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "urgent_notify");

   struct wlr_xdg_activation_v1_request_activate_event *event = data;
   struct simple_client* client = NULL;
   struct simple_layer_surface* lsurface = NULL; 
   get_client_from_surface(event->surface, &client, &lsurface); 

   struct simple_client* focused_client = get_top_client_from_output(g_server->cur_output, false);
   if(!client || client == focused_client) return;

   bool ismapped = false;
   if(client->type==XWL_MANAGED_CLIENT || client->type==XWL_UNMANAGED_CLIENT)
      ismapped = client->xwl_surface->surface->mapped;
   else 
      ismapped = client->xdg_surface->surface->mapped;

   if(ismapped)
      set_client_border_colour(client, URGENT);
   client->urgent = true;
}

//--- Lock session notify functions --------------------------------------
static void
lock_surface_destroy_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "lock_surface_destroy_notify");
   struct simple_output *output = wl_container_of(listener, output, lock_surface_destroy);
   struct wlr_session_lock_surface_v1 *lock_surface = output->lock_surface;

   output->lock_surface = NULL;
   wl_list_remove(&output->lock_surface_destroy.link);

   if(lock_surface->surface != g_server->seat->keyboard_state.focused_surface)
      return;

   if(g_server->locked && g_server->cur_lock && !wl_list_empty(&g_server->cur_lock->surfaces)){
      struct wlr_session_lock_surface_v1 *surface = wl_container_of(g_server->cur_lock->surfaces.next, surface, link);
      input_focus_surface(surface->surface);
   } else if(!(g_server->locked)){
      focus_client(get_top_client_from_output(output, false), true);
   } else {
      wlr_seat_keyboard_clear_focus(g_server->seat);
   }
}

static void
new_lock_surface_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "new_lock_surface_notify");
   struct simple_session_lock *slock = wl_container_of(listener, slock, new_surface);
   struct wlr_session_lock_surface_v1 *lock_surface = data;
   struct simple_output *output = lock_surface->output->data;
   struct wlr_scene_tree *scene_tree = wlr_scene_subsurface_tree_create(slock->scene, lock_surface->surface);
   lock_surface->surface->data = scene_tree;
   output->lock_surface = lock_surface;
   
   wlr_scene_node_set_position(&scene_tree->node, output->full_area.x, output->full_area.y);
   wlr_session_lock_surface_v1_configure(lock_surface, output->full_area.width, output->full_area.height);

   LISTEN(&lock_surface->events.destroy, &output->lock_surface_destroy, lock_surface_destroy_notify);
   
   if(output == g_server->cur_output)
      input_focus_surface(lock_surface->surface);
}

static void
unlock_session_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "unlock_session_notify");
   struct simple_session_lock *slock = wl_container_of(listener, slock, unlock);
   
   g_server->locked = false;
   wlr_seat_keyboard_notify_clear_focus(g_server->seat);

   wlr_scene_node_set_enabled(&g_server->locked_bg->node, 0);
   //focus_client()
}

static void
lock_session_destroy_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "lock_session_destroy_notify");
   struct simple_session_lock *slock = wl_container_of(listener, slock, destroy);

   wlr_seat_keyboard_notify_clear_focus(g_server->seat);

   wl_list_remove(&slock->new_surface.link);
   wl_list_remove(&slock->unlock.link);
   wl_list_remove(&slock->destroy.link);

   wlr_scene_node_destroy(&slock->scene->node);
   g_server->cur_lock = NULL;
   free(slock);
}

static void
lock_session_manager_destroy_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "lock_session_manager_destroy_notify");

   wl_list_remove(&g_server->new_lock_session_manager.link);
   wl_list_remove(&g_server->lock_session_manager_destroy.link);
}

static void
new_lock_session_manager_notify(struct wl_listener *listener, void *data)
{
   say(DEBUG, "new_lock_session_manager_notify");
   struct wlr_session_lock_v1 *session_lock = data;

   wlr_scene_node_set_enabled(&g_server->locked_bg->node, 1);
   if(g_server->cur_lock){
      wlr_session_lock_v1_destroy(session_lock);
      return;
   }

   struct simple_session_lock *slock = calloc(1, sizeof(struct simple_session_lock));
   slock->scene = wlr_scene_tree_create(g_server->layer_tree[LyrLock]);
   g_server->cur_lock = slock->lock = session_lock;
   g_server->locked = true;
   session_lock->data = slock;

   LISTEN(&session_lock->events.new_surface, &slock->new_surface, new_lock_surface_notify);
   LISTEN(&session_lock->events.unlock, &slock->unlock, unlock_session_notify);
   LISTEN(&session_lock->events.destroy, &slock->destroy, lock_session_destroy_notify);

   wlr_session_lock_v1_send_locked(session_lock);
}

//--- Output notify functions --------------------------------------------
static void 
output_layout_change_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_layout_change_notify");

   struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
   struct wlr_output_configuration_head_v1 *config_head;
   if(!config)
      say(ERROR, "wlr_output_configuration_v1_create failed");

   struct simple_output *output;
   wl_list_for_each(output, &g_server->outputs, link) {
      if(!output->wlr_output->enabled) continue;

      config_head = wlr_output_configuration_head_v1_create(config, output->wlr_output);
      if(!config_head) {
         wlr_output_configuration_v1_destroy(config);
         say(ERROR, "wlr_output_configuration_head_v1_create failed");
      }
      struct wlr_box box;
      wlr_output_layout_get_box(g_server->output_layout, output->wlr_output, &box);
      if(wlr_box_empty(&box))
         say(ERROR, "Failed to get output layout box");

      memset(&output->usable_area, 0, sizeof(output->usable_area));
      output->usable_area = box;

      output->gamma_lut_changed = true;
      config_head->state.x = box.x;
      config_head->state.y = box.y;
   }

   if(config)
      wlr_output_manager_v1_set_configuration(g_server->output_manager, config);
   else
      say(ERROR, "wlr_output_manager_v1_set_configuration failed");
}

static void 
output_manager_apply_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_manager_apply_notify");
   //
}

static void 
output_manager_test_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_manager_test_notify");
   //
}

static void 
output_frame_notify(struct wl_listener *listener, void *data) 
{
   //say(DEBUG, "output_frame_notify");
   struct simple_output *output = wl_container_of(listener, output, frame);
   struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(g_server->scene, output->wlr_output);
   
   struct wlr_gamma_control_v1 *gamma_control;
   struct wlr_output_state pending = {0};
   if (output->gamma_lut_changed) {
      say(DEBUG, "gamma_lut_changed true");
      gamma_control = wlr_gamma_control_manager_v1_get_control(g_server->gamma_control_manager, output->wlr_output);
      output->gamma_lut_changed = false;

      if(!wlr_gamma_control_v1_apply(gamma_control, &pending))
         wlr_scene_output_commit(scene_output, NULL);

      if(!wlr_output_test_state(output->wlr_output, &pending)) {
         wlr_gamma_control_v1_send_failed_and_destroy(gamma_control);
         wlr_scene_output_commit(scene_output, NULL);
      }
      wlr_output_commit_state(output->wlr_output, &pending);
      wlr_output_schedule_frame(output->wlr_output);
   } else {
      // Render the scene if needed and commit the output 
      wlr_scene_output_commit(scene_output, NULL);
   }
   
   struct timespec now;
   clock_gettime(CLOCK_MONOTONIC, &now);
   wlr_scene_output_send_frame_done(scene_output, &now);
}

static void 
output_request_state_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_request_state_notify");
   // called when the backend requests a new state for the output
   struct simple_output *output = wl_container_of(listener, output, request_state);
   const struct wlr_output_event_request_state *event = data;
   wlr_output_commit_state(output->wlr_output, event->state);
}

static void 
output_destroy_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "output_destroy_notify");
   struct simple_output *output = wl_container_of(listener, output, destroy);

   struct simple_ipc_output *ipc_output, *ipc_output_tmp;
   wl_list_for_each_safe(ipc_output, ipc_output_tmp, &output->ipc_outputs, link)
      wl_resource_destroy(ipc_output->resource);

   wl_list_remove(&output->frame.link);
   wl_list_remove(&output->request_state.link);
   wl_list_remove(&output->destroy.link);
   wl_list_remove(&output->link);
   free(output);
}

//------------------------------------------------------------------------
static void 
new_output_notify(struct wl_listener *listener, void *data) 
{
   say(DEBUG, "new_output_notify");
   struct wlr_output *wlr_output = data;

   // Don't configure any non-desktop displays, such as VR headsets
   if(wlr_output->non_desktop) {
      say(DEBUG, "Not configuring non-desktop output");
      return;
   }

   // Configures the output created by the backend to use the allocator and renderer
   // Must be done once, before committing the output
   if(!wlr_output_init_render(wlr_output, g_server->allocator, g_server->renderer))
      say(ERROR, "unable to initialize output renderer");
   
   // The output may be disabled. Switch it on
   struct wlr_output_state state;
   wlr_output_state_init(&state);
   wlr_output_state_set_enabled(&state, true);

   struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
   if (mode)
      wlr_output_state_set_mode(&state, mode);

   wlr_output_commit_state(wlr_output, &state);
   wlr_output_state_finish(&state);

   struct simple_output *output = calloc(1, sizeof(struct simple_output));
   output->wlr_output = wlr_output;
   wlr_output->data = output;

   wl_list_init(&output->ipc_outputs);   // ipc addition

   LISTEN(&wlr_output->events.frame, &output->frame, output_frame_notify);
   LISTEN(&wlr_output->events.destroy, &output->destroy, output_destroy_notify);
   LISTEN(&wlr_output->events.request_state, &output->request_state, output_request_state_notify);

   wl_list_insert(&g_server->outputs, &output->link);

   for(int i=0; i<N_LAYER_SHELL_LAYERS; i++)
      wl_list_init(&output->layer_shells[i]);
   
   wlr_scene_node_lower_to_bottom(&g_server->layer_tree[LyrBottom]->node);
   wlr_scene_node_lower_to_bottom(&g_server->layer_tree[LyrBg]->node);
   wlr_scene_node_raise_to_top(&g_server->layer_tree[LyrTop]->node);
   wlr_scene_node_raise_to_top(&g_server->layer_tree[LyrOverlay]->node);
   wlr_scene_node_raise_to_top(&g_server->layer_tree[LyrLock]->node);

   //set default tag
   output->current_tag = TAGMASK(0);
   output->visible_tags = TAGMASK(0);

   struct wlr_output_layout_output *l_output =
      wlr_output_layout_add_auto(g_server->output_layout, wlr_output);
   struct wlr_scene_output *scene_output =
      wlr_scene_output_create(g_server->scene, wlr_output);
   wlr_scene_output_layout_add_output(g_server->scene_output_layout, l_output, scene_output);

   // update background and lock geometry
   struct wlr_box geom;
   wlr_output_layout_get_box(g_server->output_layout, NULL, &geom);

   memset(&output->full_area, 0, sizeof(output->full_area));
   output->full_area = geom;
   
   wlr_scene_node_set_position(&g_server->root_bg->node, geom.x, geom.y);
   wlr_scene_rect_set_size(g_server->root_bg, geom.width, geom.height);
   wlr_scene_node_set_position(&g_server->locked_bg->node, geom.x, geom.y);
   wlr_scene_rect_set_size(g_server->locked_bg, geom.width, geom.height);

   wlr_scene_node_set_enabled(&g_server->root_bg->node, 1);

   say(INFO, " -> Output %s : %dx%d+%d+%d", l_output->output->name,
         l_output->output->width, l_output->output->height, 
         l_output->x, l_output->y);
}

//------------------------------------------------------------------------
void 
prepareServer() 
{
   say(INFO, "Preparing Wayland server");
   
   if(!(g_server->display = wl_display_create()))
      say(ERROR, "Unable to create Wayland display!");

   if(!(g_server->backend = wlr_backend_autocreate(g_server->display, &g_session)))
      say(ERROR, "Unable to create wlr_backend!");

   // create a scene graph used to lay out windows
   /* 
    * | layer      | type          | example  |
    * |------------|---------------|----------|
    * | LyrLock    | lock-manager  | swaylock |
    * | LyrOverlay | layer-shell   |          |
    * | LyrTop     | layer-shell   | waybar   |
    * | LyrClient  | normal client |          |
    * | LyrBottom  | layer-shell   |          |
    * | LyrBg      | layer-shell   | wbg      |
    */
   g_server->scene = wlr_scene_create();
   for(int i=0; i<NLayers; i++)
      g_server->layer_tree[i] = wlr_scene_tree_create(&g_server->scene->tree);

   // create renderer
   if(!(g_server->renderer = wlr_renderer_autocreate(g_server->backend)))
      say(ERROR, "Unable to create wlr_renderer");

   wlr_renderer_init_wl_display(g_server->renderer, g_server->display);
 
   // create an allocator
   if(!(g_server->allocator = wlr_allocator_autocreate(g_server->backend, g_server->renderer)))
      say(ERROR, "Unable to create wlr_allocator");

   // create compositor
   g_server->compositor = wlr_compositor_create(g_server->display, COMPOSITOR_VERSION, g_server->renderer);
   wlr_subcompositor_create(g_server->display);
   wlr_data_device_manager_create(g_server->display);
   
   wlr_export_dmabuf_manager_v1_create(g_server->display);
   wlr_screencopy_manager_v1_create(g_server->display);
   wlr_data_control_manager_v1_create(g_server->display);
   wlr_viewporter_create(g_server->display);
   wlr_single_pixel_buffer_manager_v1_create(g_server->display);
   wlr_primary_selection_v1_device_manager_create(g_server->display);
   wlr_fractional_scale_manager_v1_create(g_server->display, FRAC_SCALE_VERSION);
   
   // initialize interface used to implement urgency hints TODO
   g_server->xdg_activation = wlr_xdg_activation_v1_create(g_server->display);
   LISTEN(&g_server->xdg_activation->events.request_activate, &g_server->request_activate, urgent_notify);

   // gamma control manager
   g_server->gamma_control_manager = wlr_gamma_control_manager_v1_create(g_server->display);
   LISTEN(&g_server->gamma_control_manager->events.set_gamma, &g_server->set_gamma, set_gamma_notify);

   // create an output layout, i.e. wlroots utility for working with an arrangement of 
   // screens in a physical layout
   g_server->output_layout = wlr_output_layout_create();
   LISTEN(&g_server->output_layout->events.change, &g_server->output_layout_change, output_layout_change_notify);

   wl_list_init(&g_server->outputs);   
   LISTEN(&g_server->backend->events.new_output, &g_server->new_output, new_output_notify);

   g_server->scene_output_layout = wlr_scene_attach_output_layout(g_server->scene, g_server->output_layout);

   wlr_xdg_output_manager_v1_create(g_server->display, g_server->output_layout);
   g_server->output_manager = wlr_output_manager_v1_create(g_server->display);
   LISTEN(&g_server->output_manager->events.apply, &g_server->output_manager_apply, output_manager_apply_notify);
   LISTEN(&g_server->output_manager->events.test, &g_server->output_manager_test, output_manager_test_notify);
   
   // set up seat and inputs
   g_server->seat = wlr_seat_create(g_server->display, "seat0");
   if(!g_server->seat)
      say(ERROR, "cannot allocate seat");

   input_init();

   // set up Wayland shells, i.e. XDG, layer shell and XWayland
   wl_list_init(&g_server->clients);
   
   if(!(g_server->xdg_shell = wlr_xdg_shell_create(g_server->display, XDG_SHELL_VERSION)))
      say(ERROR, "unable to create XDG shell interface");
   LISTEN(&g_server->xdg_shell->events.new_surface, &g_server->xdg_new_surface, xdg_new_surface_notify);

   g_server->layer_shell = wlr_layer_shell_v1_create(g_server->display, LAYER_SHELL_VERSION);
   LISTEN(&g_server->layer_shell->events.new_surface, &g_server->layer_new_surface, layer_new_surface_notify);
   
   // set up idle notifier and inhibit manager
   g_server->idle_notifier = wlr_idle_notifier_v1_create(g_server->display);
   g_server->idle_inhibit_manager = wlr_idle_inhibit_v1_create(g_server->display);
   LISTEN(&g_server->idle_inhibit_manager->events.new_inhibitor, &g_server->new_inhibitor, new_inhibitor_notify);

   // set up session lock manager
   g_server->session_lock_manager = wlr_session_lock_manager_v1_create(g_server->display);
   LISTEN(&g_server->session_lock_manager->events.new_lock, &g_server->new_lock_session_manager, new_lock_session_manager_notify);
   LISTEN(&g_server->session_lock_manager->events.destroy, &g_server->lock_session_manager_destroy, lock_session_manager_destroy_notify);

   // set up output power manager
   g_server->output_power_manager = wlr_output_power_manager_v1_create(g_server->display);
   LISTEN(&g_server->output_power_manager->events.set_mode, &g_server->output_pm_set_mode, output_pm_set_mode_notify);

   // set initial size - will be updated when output is changed
   g_server->locked_bg = wlr_scene_rect_create(g_server->layer_tree[LyrLock], 1, 1, (float [4]){0.1, 0.1, 0.1, 1.0});
   wlr_scene_node_set_enabled(&g_server->locked_bg->node, 0);

   // set initial background - will be updated when output is changed
   g_server->root_bg = wlr_scene_rect_create(g_server->layer_tree[LyrBg], 1, 1, g_config->background_colour);
   wlr_scene_node_set_enabled(&g_server->root_bg->node, 0);

   // Use decoration protocols to negotiate server-side decorations
   wlr_server_decoration_manager_set_default_mode(wlr_server_decoration_manager_create(g_server->display),
         WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
   g_server->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(g_server->display);
   LISTEN(&g_server->xdg_decoration_manager->events.new_toplevel_decoration, &g_server->new_decoration, new_decoration_notify);

   struct wlr_presentation *presentation = wlr_presentation_create(g_server->display, g_server->backend);
   wlr_scene_set_presentation(g_server->scene, presentation);

   // Set up IPC interface
   wl_global_create(g_server->display, &zdwl_ipc_manager_v2_interface, DWL_IPC_VERSION, NULL, ipc_manager_bind);

#if XWAYLAND
   if(!(g_server->xwayland = wlr_xwayland_create(g_server->display, g_server->compositor, true))) {
      say(WARNING, "unable to create xwayland server. Continuing without it");
      return;
   }

   LISTEN(&g_server->xwayland->events.new_surface, &g_server->xwl_new_surface, xwl_new_surface_notify);
   LISTEN(&g_server->xwayland->events.ready, &g_server->xwl_ready, xwl_ready_notify);
#endif
}

void 
startServer(char* start_cmd) 
{
   say(INFO, "Starting Wayland server");

   const char* socket = wl_display_add_socket_auto(g_server->display);
   if(!socket){
      cleanupServer(g_server);
      say(ERROR, "Unable to add socket to Wayland display!");
   }

   if(!wlr_backend_start(g_server->backend)){
      cleanupServer(g_server);
      say(ERROR, "Unable to start WLR backend!");
   }
   
   setenv("WAYLAND_DISPLAY", socket, true);
   say(INFO, " -> Wayland server is running on WAYLAND_DISPLAY=%s ...", socket);

#if XWAYLAND
   if(setenv("DISPLAY", g_server->xwayland->display_name, true) < 0)
      say(WARNING, " -> Unable to set DISPLAY for xwayland");
   else 
      say(INFO, " -> XWayland is running on display %s", g_server->xwayland->display_name);
#endif

   // choose initial output based on cursor position
   g_server->cur_output = get_output_at(g_server->cursor->x, g_server->cursor->y);

   // Run autostarts and startup comand if defined
   if(start_cmd[0]!='\0') spawn(start_cmd);
   if(g_config->autostart_script[0]!='\0') spawn(g_config->autostart_script);
}

void 
cleanupServer() 
{
   say(INFO, "Cleaning up Wayland server");

#if XWAYLAND
   g_server->xwayland = NULL;
   wlr_xwayland_destroy(g_server->xwayland);
#endif

   wl_display_destroy_clients(g_server->display);
   wlr_xcursor_manager_destroy(g_server->cursor_manager);
   wlr_output_layout_destroy(g_server->output_layout);
   wl_display_destroy(g_server->display);
   // Destroy after the wayland display
   wlr_scene_node_destroy(&g_server->scene->tree.node);
}

