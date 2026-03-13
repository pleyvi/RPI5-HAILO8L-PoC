#!/bin/sh
g++ -O3 -Wall tracker_app.cpp -o tracker_app $(pkg-config --cflags --libs gstreamer-1.0) -I/usr/include/hailo/tappas -lgsthailometa
