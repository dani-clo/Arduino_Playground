# Camera Throughput Diagnostic

Arduino sketch and companion Python script for measuring camera capture and raw
frame transfer performance over USB serial. The sketch captures 320 × 240 RGB565
images (153,600 bytes per frame). The Python script requests frames and reports
transfer rate and latency without displaying images.

## Requirements

- A camera-equipped board with an Arduino core based on Zephyr that provides
  `camera.h` and the Zephyr video API used by this sketch. Camera support must be
  enabled for the selected board; this sketch is not portable to every Arduino
  core.
- A compatible camera and a USB data connection to your computer.
- Arduino IDE or Arduino CLI to compile and upload the sketch.
- Python 3 and `pyserial` to run [camera_throughput.py](camera_throughput.py).

## Upload the sketch

1. Open [CameraThroughputDiagnostic.ino](CameraThroughputDiagnostic/CameraThroughputDiagnostic.ino) in
   Arduino IDE.
2. Select the board and its serial port.
3. Set `CAMERA_BENCH_MODE` near the top of the sketch to one of the values below.
4. Compile and upload. Upload again whenever you change the mode.

| Mode | Behavior | How to use it |
| --- | --- | --- |
| `0` (default) | Captures frames and sends a frame when the computer requests one. | Run the Python script. |
| `1` | Captures and releases frames without sending image data. | Open Serial Monitor at 115200 baud. |
| `2` | Captures one frame, stops the camera stream, and sends that same frame for every request. | Run the Python script; the image remains fixed until reset. |

The sketch waits for the serial connection to open before initializing the
camera. Use only one program at a time to access the port. Close Serial Monitor
and any camera viewer before running the Python script.

In mode `1`, Serial Monitor prints `frames`, `elapsed_ms`, and `grab_failures`
approximately every five seconds. The capture rate for each interval is
`frames * 1000 / elapsed_ms`; `grab_failures` counts unsuccessful frame capture
calls.

## Run the Python script

Run these commands from the repository root, in a Python environment of your
choice:

```sh
python3 -m pip install pyserial
python3 CameraThroughputMeasure/camera_throughput.py /dev/ttyACM0 --frames 30
```

Replace `/dev/ttyACM0` with your board's port. On Windows, for example:

```powershell
python CameraThroughputMeasure/camera_throughput.py COM3 --frames 30
```

If your terminal is already in the `CameraThroughputMeasure` directory, use:

```sh
python3 camera_throughput.py /dev/ttyACM0 --frames 30
```

| Argument | Description | Default |
| --- | --- | --- |
| `port` | Serial port connected to the board (required). | — |
| `--frames` | Number of frames to measure after warmup; must be positive. | `30` |
| `--bytes` | Expected bytes per frame; must be positive. | `153600` |

Keep the default `--bytes` value for the unmodified sketch. This option only
changes how many bytes the script expects; it does not configure the camera.

The script opens the port at 115200 baud, waits two seconds, and discards three
warmup frames before collecting measurements. It sends one request at a time
and exits if a frame takes longer than about ten seconds to arrive.

## Output

At the end of a run, the Python script prints two lines in the terminal, for example:

```text
Frames: 30, FPS: 4.23, payload: 0.650 MB/s
Frame latency ms: min=234.5, median=236.2, max=238.1
```

Values vary with the board, camera, and USB connection.

The script reports:

- **Frames:** number of measured frames, excluding warmup.
- **FPS:** reciprocal of the mean request-to-complete-frame time.
- **Payload MB/s:** image bytes received per second, in decimal megabytes.
- **Frame latency:** minimum, median, and maximum request-to-complete-frame time
  in milliseconds.

These measurements include frame availability, serial transfer, and computer
processing overhead; they do not directly measure the camera sensor's frame
rate. For comparable runs, use the same camera settings, lighting, USB cable,
computer, and frame count.

## Serial protocol

In modes `0` and `2`, the computer sends the single byte `0x01` to request a
frame. The sketch replies with the raw RGB565 frame bytes, without a header,
length field, or checksum. Keep the USB serial stream free of additional text
while transferring images.
