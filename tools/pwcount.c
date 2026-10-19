/* owbox-pwcount FD NODE SECONDS [TARGET]: count the video frames a PipeWire
   node sends, for testing screen sharing (see tools/screencast-test.py).
   FD is a PipeWire remote (-1 for the default), TARGET the node's
   object.serial. */
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/debug/types.h>
#include <spa/param/video/type-info.h>
#include <stdio.h>
#include <stdlib.h>

struct data { struct pw_main_loop *loop; struct pw_stream *stream;
              int frames, empty; struct spa_video_info_raw fmt; };

static void on_process(void *d)
{
    struct data *data = d;
    struct pw_buffer *b = pw_stream_dequeue_buffer(data->stream);
    if (!b) return;
    if (b->buffer->datas[0].chunk->size == 0) data->empty++;
    else data->frames++;
    pw_stream_queue_buffer(data->stream, b);
}

static void on_param_changed(void *d, uint32_t id, const struct spa_pod *param)
{
    struct data *data = d;
    if (!param || id != SPA_PARAM_Format) return;
    spa_format_video_raw_parse(param, &data->fmt);
    fprintf(stderr, "format %s %ux%u\n",
            spa_debug_type_find_name(spa_type_video_format, data->fmt.format),
            data->fmt.size.width, data->fmt.size.height);
}

static void on_state(void *d, enum pw_stream_state old,
                     enum pw_stream_state state, const char *error)
{
    fprintf(stderr, "state %s%s%s\n", pw_stream_state_as_string(state),
            error ? " " : "", error ? error : "");
}

static const struct pw_stream_events events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state,
    .param_changed = on_param_changed,
    .process = on_process,
};

static void quit(void *d, uint64_t e) { pw_main_loop_quit(((struct data*)d)->loop); }

int main(int argc, char **argv)
{
    struct data data = {0};
    uint8_t buf[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    const struct spa_pod *params[1];
    struct pw_context *ctx; struct pw_core *core;
    int fd = atoi(argv[1]); uint32_t node = atoi(argv[2]);

    pw_init(&argc, &argv);
    data.loop = pw_main_loop_new(NULL);
    ctx = pw_context_new(pw_main_loop_get_loop(data.loop), NULL, 0);
    core = fd < 0 ? pw_context_connect(ctx, NULL, 0) : pw_context_connect_fd(ctx, fd, NULL, 0);
    if (!core) { fprintf(stderr, "connect failed\n"); return 1; }
    data.stream = pw_stream_new(core, "pwcount",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
                          "Capture", PW_KEY_MEDIA_ROLE, "Screen", PW_KEY_TARGET_OBJECT, argc > 4 ? argv[4] : argv[2],
                          NULL));
    pw_stream_add_listener(data.stream, &(struct spa_hook){0}, &events, &data);
    params[0] = spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(5,
            SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_RGBx,
            SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBA),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(
            &SPA_RECTANGLE(320, 240), &SPA_RECTANGLE(1, 1),
            &SPA_RECTANGLE(8192, 8192)),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
            &SPA_FRACTION(30, 1), &SPA_FRACTION(0, 1), &SPA_FRACTION(240, 1)));
    pw_stream_connect(data.stream, PW_DIRECTION_INPUT, node,
                      PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS,
                      params, 1);
    pw_loop_add_timer(pw_main_loop_get_loop(data.loop), quit, &data);
    {
        struct spa_source *t = pw_loop_add_timer(pw_main_loop_get_loop(data.loop), quit, &data);
        struct timespec ts = { atoi(argv[3]), 0 };
        pw_loop_update_timer(pw_main_loop_get_loop(data.loop), t, &ts, NULL, false);
    }
    pw_main_loop_run(data.loop);
    printf("frames %d empty %d\n", data.frames, data.empty);
    return 0;
}
