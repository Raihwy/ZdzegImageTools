# Zdzeg Image Tools

**Zdzeg Image Tools** is an **extremely experimental** set of command-line utilities written in C for encoding and viewing images in my custom `.zdzeg` format. It uses SDL2 for displaying images and zlib for compression.  

### Features
- Encode standard images into `.zdzeg` format
- View `.zdzeg` images with a keyboard-controlled viewer
- Adjustable color channels: red, green, blue, grayscale, or full color
- Selectable color levels: 4, 8, 16, or 32
- Designed for images sized **400×240**  

### Important Notes
- Currently has **no real practical use**
- Extremely limited functionality; mostly for experimentation and testing
- May break, crash, **could waste your time**, or do nothing useful
- **Very experimental**: only tested on Arch Linux with an i5-7200U CPU, GTX 950M GPU, 8GB RAM, and a slow 1TB HDD

## Local Compilation and Installation

Before use, you need to compile it.  
First, install the necessary development libraries.

### Debian / Ubuntu
```bash
sudo apt update
sudo apt install libsdl2-dev libsdl2-ttf-dev zlib1g-dev build-essential
```

### Fedora
```bash
sudo dnf install SDL2-devel SDL2_ttf-devel zlib-devel gcc make
```

### Arch Linux
```bash
sudo pacman -S sdl2 sdl2_ttf zlib base-devel
```

## Compiling the Programs

Once the libraries are installed, you can compile both programs from their respective C files using gcc.

### Zdzeg Viewer
Save the code in a file named `ZdzegViewer.c` and run:
```bash
gcc -o ZdzegViewer ZdzegViewer.c `pkg-config --cflags --libs sdl2 SDL2_ttf` -lz
```

### Zdzeg Encoder
Save the encoder source code in a file named `ZdzegEncoder.c` and run:
```bash
gcc -o ZdzegEncoder ZdzegEncoder.c `pkg-config --cflags --libs sdl2 SDL2_ttf` -lz
```

## Using the Zdzeg Viewer

Run the viewer and provide the path to the folder containing your `.zdzeg` files:
```bash
./ZdzegViewer /path/to/your/folder
```

Example:
```bash
./ZdzegViewer images
```

### Viewer Controls
```text
    Left / Right Arrow: Move between images.
    Up / Down Arrow:
        Use these to select a folder or file.
        When viewing an image, they zoom in and out.
    Enter: Opens the selected folder.
    X: Works as a back button to go up one folder level.
    R: Rotates the image 90° clockwise.
    F: Toggles full-screen.
    H: Toggles "fit-to-screen" view.
    W A S D: Pans the image.
    Q or Esc: Quit.
```

## Using the Zdzeg Encoder

The encoder converts images into `.zdzeg` format.  
It takes an input file or a folder, a number of color levels, and a color channel.

- Color levels: 4, 8, 16, 32  (Don't go lower than 8 if you want decent quality)  
- Color channels: red, green, blue, bw, full  
- **Batch mode**: if you provide a folder instead of a single file, all images in the folder will be converted

Run the encoder like this:
```bash
./ZdzegEncoder <input_image_file_or_folder> <levels> <channel>
```

Examples:

Single file:
```bash
./ZdzegEncoder my_picture.png 16 red
```

Batch folder:
```bash
./ZdzegEncoder images/ 16 full
```

This will generate files such as:
```text
my_picture_16_red.zdzeg
other_image_16_full.zdzeg
...

