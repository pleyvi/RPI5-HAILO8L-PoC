#include <gst/gst.h>
#include <iostream>
#include <string>
#include <memory>
#include <chrono>

#include "hailo_objects.hpp"
#include "hailo_common.hpp"
#include "gst_hailo_meta.hpp"

// FPS Tracking Globals
static auto last_time = std::chrono::high_resolution_clock::now();
static int frame_count = 0;

// The Bus Watcher: Catches and prints pipeline errors (e.g., MediaMTX is down)
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *)data;
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            std::cout << "End of stream reached." << std::endl;
            g_main_loop_quit(loop);
            break;
        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;
            gst_message_parse_error(msg, &error, &debug);
            std::cerr << "\n[PIPELINE ERROR] " << GST_OBJECT_NAME(msg->src) << ": " << error->message << std::endl;
            if (debug) std::cerr << "Debug details: " << debug << std::endl;
            g_error_free(error);
            g_free(debug);
            g_main_loop_quit(loop);
            break;
        }
        default:
            break;
    }
    return TRUE;
}

static GstPadProbeReturn tracker_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
    GstBuffer *buffer = gst_pad_probe_info_get_buffer(info);
    if (!buffer) return GST_PAD_PROBE_OK;

    // 1. Calculate and Print FPS globally (Proves the pipeline is actively flowing)
    frame_count++;
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = now - last_time;
    if (elapsed.count() >= 1.0) {
        int current_fps = frame_count / elapsed.count();
        std::cout << "[SYSTEM] Pipeline Active | Streaming at " << current_fps << " FPS" << std::endl;
        frame_count = 0;
        last_time = now;
    }

    // 2. Extract Metadata safely (false = do not create if missing)
    HailoROIPtr roi = get_hailo_main_roi(buffer, false);
    if (!roi) return GST_PAD_PROBE_OK;

    // 3. Process Detections
    for (const auto& obj : roi->get_objects_typed(HAILO_DETECTION)) {
        HailoDetectionPtr detection = std::dynamic_pointer_cast<HailoDetection>(obj);
        if (!detection) continue;

        std::string label = detection->get_label();
        float confidence = detection->get_confidence();
        HailoBBox bbox = detection->get_bbox();

        int track_id = 0;
        for (const auto& track_obj : detection->get_objects_typed(HAILO_UNIQUE_ID)) {
            HailoUniqueIDPtr id_obj = std::dynamic_pointer_cast<HailoUniqueID>(track_obj);
            if (id_obj) {
                track_id = id_obj->get_id();
                break;
            }
        }

        // 4. Print the high-confidence tracked objects
        if (track_id > 0) {
            float x_center = bbox.xmin() + (bbox.width() / 2.0f);
            float y_center = bbox.ymin() + (bbox.height() / 2.0f);
            
            std::cout << "  -> [Track ID: " << track_id << "] " << label 
                      << " (" << confidence << ") | Center X: " << x_center 
                      << ", Y: " << y_center << std::endl;
        }
    }

    return GST_PAD_PROBE_OK;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);

    const char* home_dir = getenv("HOME");
    std::string hef_path = std::string(home_dir) + "/hailo_workspace/models/yolov8n.hef";

    std::string pipeline_str = 
    "hailomuxer name=hmux "
        "libcamerasrc camera-name=/base/axi/pcie@1000120000/rp1/i2c@80000/imx708@1a ! video/x-raw,format=NV12,width=1920,height=1080,framerate=25/1 ! tee name=t "
        "t. ! queue max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! hmux.sink_0 "
        "t. ! queue max-size-buffers=3 max-size-bytes=0 max-size-time=0 ! "
        "videoscale method=0 ! video/x-raw,format=NV12,width=640,height=640 ! "
        "videoconvert ! video/x-raw,format=RGB,pixel-aspect-ratio=1/1 ! "
        "hailonet hef-path=" + hef_path + " batch-size=1 nms-score-threshold=0.75 ! "
        "hailofilter so-path=/usr/lib/aarch64-linux-gnu/hailo/tappas/post_processes/libyolo_hailortpp_post.so qos=false ! "
        "hailotracker name=tracker keep-past-metadata=true ! "
        "hmux.sink_1 "
        "hmux. ! queue ! hailooverlay ! queue ! videoconvert ! x264enc bitrate=4000 speed-preset=ultrafast tune=zerolatency threads=4 ! h264parse ! queue ! rtspclientsink location=rtsp://localhost:8554/mystream";

    GstElement *pipeline = gst_parse_launch(pipeline_str.c_str(), nullptr);
    if (!pipeline) {
        std::cerr << "Fatal: Pipeline parsing failed. Check syntax." << std::endl;
        return -1;
    }

    // Attach the Bus Watcher to catch errors
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    // Attach the Pad Probe
    GstElement *tracker = gst_bin_get_by_name(GST_BIN(pipeline), "tracker");
    if (tracker) {
        GstPad *src_pad = gst_element_get_static_pad(tracker, "src");
        gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, tracker_probe_callback, nullptr, nullptr);
        gst_object_unref(src_pad);
        gst_object_unref(tracker);
    }

    std::cout << "Starting Advanced C++ Tracker Pipeline (IMX708)..." << std::endl;
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);

    return 0;
}
