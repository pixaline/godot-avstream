# FFAVStream FFmpeg implementation for Godot 3.5
I use this for in-game media streaming utility in my game. By default supports rtsp streams, vpx/opus.

## Installation
Clone the repository and install local versions of libvpx, libopus and ffmpeg through

`./build_avstream.sh [linux32|linux64|windows32|windows64|osx32|osx64]`

Then rename `output` folder to `ffmpeg` to be picked up by the module script and use the `export` libraries for distribution.

## Usage

Check StreamViewer.gd for an implementation that uses RectTexture and AudioStreamPlayer.
