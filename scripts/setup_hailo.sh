#!/bin/bash
# Stop on error
set -euo pipefail

# Ensure the script is run as root for system-wide installations
if [ "$EUID" -ne 0 ]; then
  echo "Please run this script with sudo."
  exit 1
fi

# Get the actual user's home directory (since sudo changes $HOME to /root)
ACTUAL_USER=$(SUDO_USER:-$USER)
USER_HOME=$(eval echo ~$ACTUAL_USER)

echo "--- 1. System & EEPROM Update ---"
apt update
apt full-upgrade -y
rpi-eeprom-update -a

echo "--- 2. Core Dependencies & GStreamer Plugins ---"
apt install -y wget curl git jq tar dkms linux-headers-$(uname -r)
apt install -y gstreamer1.0-plugins-bad gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav gstreamer1.0-tools libgstreamer1.0-dev \
    gstreamer1.0-libcamera gstreamer1.0-rtsp

echo "--- 3. Hailo Drivers & Trixie 6.12 Kernel Patch ---"
apt install -y hailo-all
# Apply the 4k page size patch required for Pi 5 Kernel 6.12
echo 'options hailo_pci force_desc_page_size=4096' > /etc/modprobe.d/hailo_pci.conf
# Reinstall the PCIe driver via DKMS now that headers and patch are present
apt install --reinstall -y hailort-pcie-driver

echo "--- 4. Enable PCIe Gen 3 ---"
CONFIG_FILE="/boot/firmware/config.txt"
if ! grep -q "^dtparam=pciex1_gen=3" "$CONFIG_FILE"; then
    echo "dtparam=pciex1_gen=3" >> "$CONFIG_FILE"
fi

echo "--- 5. Setup MediaMTX Server ---"
mkdir -p "$USER_HOME/mediamtx"
# Using jq to safely parse the JSON and extract the raw URL without quotes
MEDIAMTX_LATEST_URL=$(curl -s https://api.github.com/repos/bluenviron/mediamtx/releases/latest | jq -r '.assets[] | select(.name | endswith("linux_arm64.tar.gz")) | .browser_download_url')
wget -q "$MEDIAMTX_LATEST_URL" -O "$USER_HOME/mediamtx/mediamtx.tar.gz"
tar -xzf "$USER_HOME/mediamtx/mediamtx.tar.gz" -C "$USER_HOME/mediamtx"
rm "$USER_HOME/mediamtx/mediamtx.tar.gz"

echo "--- 6. Setup Hailo Workspace & Download Model ---"
mkdir -p "$USER_HOME/hailo_workspace/models"
wget -q https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v2.14.0/hailo8l/yolov8n.hef -O "$USER_HOME/hailo_workspace/models/yolov8n.hef"
wget -q https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v2.14.0/hailo8l/yolov8s.hef -O "$USER_HOME/hailo_workspace/models/yolov8s.hef"

echo "--- 7. Generate Run Scripts ---"
# Generate the MediaMTX startup script
cat << 'EOF' > "$USER_HOME/run_media.sh"
#!/bin/bash
cd ~/mediamtx
./mediamtx
EOF

# Generate the GStreamer pipeline script
cat << EOF > "$USER_HOME/run_stream.sh"
#!/bin/bash
gst-launch-1.0 hailomuxer name=hmux libcamerasrc ! video/x-raw,format=NV12,width=1920,height=1080,framerate=30/1 ! tee name=t t. ! queue ! hmux.sink_0 t. ! queue ! videoconvert ! videoscale add-borders=false method=0 ! video/x-raw,format=RGB,width=640,height=640,pixel-aspect-ratio=1/1 ! hailonet hef-path=$USER_HOME/hailo_workspace/models/yolov8s.hef batch-size=1 ! hailofilter so-path=/usr/lib/aarch64-linux-gnu/hailo/tappas/post_processes/libyolo_hailortpp_post.so qos=false ! hmux.sink_1 hmux. ! hailooverlay ! videoconvert ! x264enc bitrate=4000 speed-preset=ultrafast tune=zerolatency ! h264parse ! rtspclientsink location=rtsp://localhost:8554/mystream
EOF

# Fix ownership and permissions
chown -R $ACTUAL_USER:$ACTUAL_USER "$USER_HOME/mediamtx"
chown -R $ACTUAL_USER:$ACTUAL_USER "$USER_HOME/hailo_workspace"
chown $ACTUAL_USER:$ACTUAL_USER "$USER_HOME/run_media.sh"
chown $ACTUAL_USER:$ACTUAL_USER "$USER_HOME/run_stream.sh"
chmod +x "$USER_HOME/run_media.sh"
chmod +x "$USER_HOME/run_stream.sh"

echo "--- Setup Complete! ---"
echo "Please reboot your Raspberry Pi now to apply the EEPROM update and load the DKMS driver."
