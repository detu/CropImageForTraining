Utility program to crop 640 x 640 image from a full size image.

The GUI is built with Qt 6 (Widgets). OpenCV is no longer required.

## Build

Requires CMake >= 3.16 and Qt 6 (e.g. `sudo apt install qt6-base-dev`;
install `qt6-image-formats-plugins` for TIFF/WebP support).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Usage

```bash
./build/ImageCropper <input_directory> [output_directory]
```

The output directory defaults to `cropped_images/` in the current directory.
Images smaller than 640 x 640 are skipped.

| Input                  | Action                                    |
|------------------------|-------------------------------------------|
| Mouse move             | Move the 640 x 640 crop box               |
| Left click             | Save the crop (repeat for more crops)     |
| Mouse wheel            | Zoom in / out around the mouse cursor     |
| + / -                  | Zoom in / out                             |
| 0 or F                 | Fit the whole image to the window         |
| 1                      | Zoom to 100% (1 image px = 1 screen px)   |
| Right or middle drag   | Pan the zoomed image                      |
| Space / Enter / N      | Next image                                |
| Backspace / P          | Previous image                            |
| Esc                    | Skip the current image                    |
| Q                      | Quit                                      |

Each image opens scaled to fit the window. Zooming only changes the view:
crops are always 640 x 640 pixels of the original image. The current zoom
level is shown in the status bar. Crops already saved from the current image are shown as dashed
yellow boxes.

## Output naming

One image can be cropped multiple times:

| Crop   | File name              |
|--------|------------------------|
| 1st    | `crop_<name>.<ext>`    |
| 2nd    | `crop_<name>_1.<ext>`  |
| 3rd    | `crop_<name>_2.<ext>`  |
| 4th    | `crop_<name>_3.<ext>`  |

Existing files are never overwritten: numbering continues from the next free
number, including across program runs.
