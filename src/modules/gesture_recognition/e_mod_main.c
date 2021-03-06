#include <e.h>
#include <Eina.h>
#include <libinput.h>
#include <grp.h>
#include <sys/types.h>
#include <pwd.h>

E_API E_Module_Api e_modapi =
   {
      E_MODULE_API_VERSION,
      "Gesture Recognition"
   };

static struct libinput *gesture_recognition_ctx;
static Ecore_Fd_Handler *fd_listener;
static Eina_Hash *active_gestures;

typedef struct {
   Eina_Vector2 pos;
   unsigned int fingers;
   struct {
     Evas_Object *visuals, *win;
   } visuals;
} Swipe_Stats;

static int
open_restricted(const char *path, int flags, void *user_data EINA_UNUSED)
{
        int fd = open(path, flags);
        return fd < 0 ? -errno : fd;
}

static void
close_restricted(int fd, void *user_data EINA_UNUSED)
{
        close(fd);
}

static const struct libinput_interface interface = {
        .open_restricted = open_restricted,
        .close_restricted = close_restricted,
};

static void
_find_all_touch_input_devices(const char *path, struct libinput *li)
{
   Eina_File_Direct_Info *info;
   Eina_Iterator *input_devies = eina_file_direct_ls(path);

   EINA_ITERATOR_FOREACH(input_devies, info)
     {
        struct libinput_device *dev = libinput_path_add_device(li, info->path);

        if (!dev) continue;

        if (!libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_GESTURE))
          {
             libinput_path_remove_device(dev);
          }
     }
}

static Swipe_Stats*
_find_swipe_gesture_recognizition(struct libinput_device *dev)
{
   Swipe_Stats *stats = eina_hash_find(active_gestures, dev);

   return stats;
}

static Swipe_Stats*
_start_swipe_gesture_recognizition(struct libinput_device *dev)
{
   Swipe_Stats *stats = _find_swipe_gesture_recognizition(dev);

   if (stats)
     eina_hash_del_by_key(active_gestures, dev);

   stats = calloc(1, sizeof(Swipe_Stats));
   EINA_SAFETY_ON_NULL_RETURN_VAL(stats, NULL);

   if (e_bindings_swipe_available())
     {
        E_Zone *zone = e_zone_current_get();

        stats->visuals.win =  elm_notify_add(zone->base);
        elm_notify_align_set(stats->visuals.win, 0.5, 0.5);
        elm_object_tree_focus_allow_set(stats->visuals.win, EINA_FALSE);
        evas_object_layer_set(stats->visuals.win, E_LAYER_CLIENT_PRIO);
        evas_object_show(stats->visuals.win);

        stats->visuals.visuals = elm_progressbar_add(stats->visuals.win);
        elm_object_text_set(stats->visuals.visuals, "Progress of visuals");
        evas_object_size_hint_min_set(stats->visuals.visuals, 300, 50);
        evas_object_show(stats->visuals.visuals);
        elm_object_content_set(stats->visuals.win, stats->visuals.visuals);
     }


   eina_hash_add(active_gestures, dev, stats);

   return stats;
}

static void
_end_swipe_gesture_recognizition(struct libinput_device *dev)
{
   eina_hash_del_by_key(active_gestures, dev);
}

static double
_config_angle(Eina_Vector2 pos)
{
   double res = atan(pos.y/pos.x);

   if (res < 0) res += M_PI;
   if (pos.y < 0) res += M_PI;
   return res;
}

static Eina_Bool
_cb_input_dispatch(void *data, Ecore_Fd_Handler *hdlr EINA_UNUSED)
{
   struct libinput *li = data;
   struct libinput_event *event;

   if (libinput_dispatch(li) != 0)
     printf("Failed to dispatch libinput events");

   while((event = libinput_get_event(li)))
     {
        E_Bindings_Swipe_Live_Update live_update = e_bindings_swipe_live_update_hook_get();

        enum libinput_event_type type = libinput_event_get_type(event);
             struct libinput_device *dev = libinput_event_get_device(event);
        if (type == LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN)
          {
             struct libinput_event_gesture *gesture = libinput_event_get_gesture_event(event);

             Swipe_Stats *stats = _start_swipe_gesture_recognizition(dev);
             stats->fingers = libinput_event_gesture_get_finger_count(gesture);
             stats->pos.x = stats->pos.y = 0;
          }
        else if (type == LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE)
          {
             struct libinput_event_gesture *gesture = libinput_event_get_gesture_event(event);
             Swipe_Stats *stats = _find_swipe_gesture_recognizition(dev);

             stats->pos.x += libinput_event_gesture_get_dx(gesture);
             stats->pos.y += libinput_event_gesture_get_dy(gesture);
             if (live_update)
               {
                  live_update(e_bindings_swipe_live_update_hook_data_get(), EINA_FALSE, _config_angle(stats->pos), eina_vector2_length_get(&stats->pos), 0.8, stats->fingers);
               }
             else if (stats->visuals.win)
               {
                  Eina_Inarray *res = e_bindings_swipe_find_candidates(E_BINDING_CONTEXT_NONE, _config_angle (stats->pos), eina_vector2_length_get(&stats->pos), stats->fingers);
                  E_Binding_Swipe_Candidate *itr;
                  double total = 0.0f;
                  unsigned int len = 0;

                  EINA_INARRAY_FOREACH(res, itr)
                    {
                       total += itr->acceptance;
                       len ++;
                    }

                  if (len > 0)
                    {
                       char text_buffer[1000];

                       snprintf(text_buffer, sizeof(text_buffer), "%d gestures possible", len);
                       elm_progressbar_value_set(stats->visuals.visuals, total/len);
                       elm_object_text_set(stats->visuals.visuals, text_buffer);
                    }
                  else
                    {
                       elm_progressbar_value_set(stats->visuals.visuals, 0.0f);
                       elm_object_text_set(stats->visuals.visuals, "No gesture found");
                    }

                  eina_inarray_free(res);
               }
          }
        else if (type == LIBINPUT_EVENT_GESTURE_SWIPE_END)
          {
             Swipe_Stats *stats = _find_swipe_gesture_recognizition(dev);

             if (live_update)
               live_update(e_bindings_swipe_live_update_hook_data_get(), EINA_TRUE, _config_angle(stats->pos), eina_vector2_length_get(&stats->pos), 0.8, stats->fingers);
             else
               e_bindings_swipe_handle(E_BINDING_CONTEXT_NONE, NULL, _config_angle(stats->pos), eina_vector2_length_get(&stats->pos), stats->fingers);

             _end_swipe_gesture_recognizition(dev);
          }
        libinput_event_destroy(event);
     }
   return EINA_TRUE;
}

static void
_setup_libinput(void)
{
   gesture_recognition_ctx = libinput_path_create_context(&interface, NULL);

   _find_all_touch_input_devices("/dev/input/", gesture_recognition_ctx);

   fd_listener = ecore_main_fd_handler_add(libinput_get_fd(gesture_recognition_ctx), ECORE_FD_READ, _cb_input_dispatch, gesture_recognition_ctx, NULL, NULL);
}


static void
_tear_down_libinput(void)
{
   ecore_main_fd_handler_del(fd_listener);
   libinput_unref(gesture_recognition_ctx);
}

static Eina_Bool
_user_part_of_input(void)
{
   uid_t user = getuid();
   struct passwd *user_pw = getpwuid(user);
   gid_t *gids = NULL;
   int number_of_groups = 0;
   struct group *input_group = getgrnam("input");

   EINA_SAFETY_ON_NULL_RETURN_VAL(user_pw, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(input_group, EINA_FALSE);

   if (getgrouplist(user_pw->pw_name, getgid(), NULL, &number_of_groups) != -1)
     {
        ERR("Failed to enumerate groups of user");
        return EINA_FALSE;
     }
   number_of_groups ++;
   gids = alloca((number_of_groups) * sizeof(gid_t));
   if (getgrouplist(user_pw->pw_name, getgid(), gids, &number_of_groups) == -1)
     {
        ERR("Failed to get groups of user");
        return EINA_FALSE;
     }

   for (int i = 0; i < number_of_groups; ++i)
     {
        if (gids[i] == input_group->gr_gid)
          return EINA_TRUE;
     }
   return EINA_FALSE;
}

static void
_stats_free(void *ptr)
{
   Swipe_Stats *stats = ptr;

   evas_object_del(stats->visuals.win);
   free(stats);
}

E_API int
e_modapi_init(E_Module *m EINA_UNUSED)
{
   if (!_user_part_of_input())
     {
        e_module_dialog_show(m, "Gesture Recognition", "Your user is not part of the input group, libinput cannot be used.");

        return 0;
     }
   active_gestures = eina_hash_pointer_new(_stats_free);
   _setup_libinput();

   return 1;
}

E_API int
e_modapi_shutdown(E_Module *m EINA_UNUSED)
{
   _tear_down_libinput();
   return 1;
}

E_API int
e_modapi_save(E_Module *m EINA_UNUSED)
{

   return 1;
}

