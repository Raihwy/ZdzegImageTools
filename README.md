Compiling Zdzeg Tools

First, you need to install the necessary development libraries for SDL2 and zlib using your distribution's package manager.

    Debian/Ubuntu

    sudo apt update
    sudo apt install libsdl2-dev zlib1g-dev

    Fedora

    sudo dnf install SDL2-devel zlib-devel

    Arch Linux

    sudo pacman -S sdl2 zlib

Once the libraries are installed, you can compile both programs from their respective C files.

    Compiling the Zdzeg Viewer
    Save the viewer code in a file named ZdzegViewer.c. Then, compile it with this command:

    gcc -o ZdzegViewer ZdzegViewer.c `pkg-config --cflags --libs sdl2` -lz

    This will create an executable file named ZdzegViewer.

    Compiling the Zdzeg Encoder
    Save the encoder source code in a file named ZdzegEncoder.c. Then, compile it with this command:

    gcc -o ZdzegEncoder ZdzegEncoder.c `pkg-config --cflags --libs sdl2` -lz

    This will create a separate executable file named ZdzegEncoder.

How to Use the Zdzeg Viewer

To run the viewer, you must provide the path to the folder containing your .zdzeg files as the first command-line argument.

./ZdzegViewer /path/to/your/folder

For example, if you have a folder named images in your current directory, you would run:

./ZdzegViewer images

Once the viewer is running, you can use these keyboard controls to navigate:

    Left Arrow (<-): View the previous image.

    Right Arrow (->): View the next image.

    R: Rotate the current image 90 degrees clockwise.

    F: Toggle full-screen mode.

    H: Toggle "fit to screen" mode.

    W A S D: Pan the image when zoomed in.

    Up Arrow: Zoom in.

    Down Arrow: Zoom out.

    Q or Escape: Quit the application.

How to Use the Zdzeg Encoder

The encoder is a tool used to convert source images into the .zdzeg format. It takes an input file, a comma-separated list of color levels, and a comma-separated list of color channels to produce the output files.

The available color levels are: 4, 8, 16, and 32.
The available color channels are: red, green, blue, bw, and full.

./ZdzegEncoder <input_image_file> <levels> <channels>

For example, to convert a file named my_picture.png using 16 levels for the red channel, you would run:

./ZdzegEncoder my_picture.png 16 red

This will generate a new file based on your input, such as my_picture_16_red.zdzeg.
