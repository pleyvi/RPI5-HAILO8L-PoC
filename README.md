# RPI5-HAILO8L-PoC
This repo is about Proof-of-Concept perfomance of the Raspberry Pi 5 + AI+ HAT + Raspberry Pi 3 camera in Yolo v8s/n + object tracking and filtering

Proof of Concept: High-Performance Edge AI Tracking Pipeline

Executive Summary
Engineered a production-ready, highly optimized object detection and tracking pipeline for edge devices. The system leverages hardware-accelerated neural network inference and custom multi-threaded C++ GStreamer architecture to achieve sustained real-time performance (25 FPS) with minimal CPU overhead, ensuring temporal consistency for non-rigid body tracking.

Hardware Stack

Compute: ARM Cortex-A76 architecture (Raspberry Pi 5)

AI Accelerator: Hailo-8L NPU (via PCIe Gen 2)

Sensor: Sony IMX708 (1080p hardware-locked via libcamera backend)

Software & Architecture

Core: C++17, GStreamer 1.0, Hailo Tappas SDK.

Inference Model: YOLOv8n (Cross-compiled to Hailo HEF format).

Temporal Tracking: ByteTrack (Kalman Filter + Bipartite Matching) handling frame-to-frame identity persistence and occlusion recovery.

Engineering Optimizations Implemented

Zero-Copy Hardware NMS: Pushed the Non-Maximum Suppression (NMS) and 75% confidence thresholding directly to the NPU (nms-score-threshold=0.75), preventing the PCIe bus and host CPU from wasting cycles on low-confidence background noise.

Order-of-Operations Pipeline Flip: Eliminated a monolithic CPU bottleneck by scaling the raw 1080p NV12 buffer before executing the RGB color space conversion. This mathematically removed ~80% of the CPU matrix operations.

Explicit Multi-Threading: Engineered aggressive thread boundaries (queue elements) to decouple video ingestion, AI inference, metadata overlay, and H.264 software encoding, allowing the Linux scheduler to evenly distribute the workload.

Performance Metrics

Throughput: Sustained 25 FPS (locked to target).

CPU Utilization: Evenly distributed across 4 cores (~22%, 22%, 30%, 14%).

System Load Average: Maintained strictly below 1.0 during active tracking.
