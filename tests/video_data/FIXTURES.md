# ZCSR video T0 fixtures

Two real encoded streams, generated with the bundled FFmpeg prebuilt
(`third_party/prebuilt/win64/ffmpeg/bin/ffmpeg.exe`, n8.1.3 LGPL shared).
Both are 64×64, 12 fps, 4 frames, audio disabled, well under the 64 KB cap.

## Frame content (identical in both fixtures)

- Background: red (`#FF0000`).
- Green block: top-right, x[48,64) y[0,16).
- Blue block: bottom half, x[0,64) y[32,64).

Assertion sample points used by the tests:

| Block | Sample pixel (x, y) | Dominant channel |
|---|---|---|
| red   | (8, 8)   | R |
| green | (56, 8)  | G |
| blue  | (32, 48) | B |

Lossy YUV → RGB conversion leaves ±10 channel deviation; tests assert on the
dominant channel with a 30-channel margin, not exact RGB values.

## `t0_h264.mp4` — MP4 / H.264

```
ffmpeg -y -hide_banner \
  -f lavfi -i "color=c=red:s=64x64:r=12:d=1" \
  -vf "drawbox=x=48:y=0:w=16:h=16:color=green:t=fill,drawbox=x=0:y=32:w=64:h=32:color=blue:t=fill" \
  -frames:v 4 -c:v libopenh264 -pix_fmt yuv420p -an \
  tests/video_data/t0_h264.mp4
```

- Codec: H.264 (AVC), `yuv420p`; container ISO BMFF (`mov,mp4,...`).
- This LGPL build has **no libx264**; H.264 encoding uses `libopenh264`.
  The bundled openh264 build rejects `-profile:v baseline` ("Unable to parse
  profile option value"), so the profile option is omitted and the encoder
  default profile is used.
- Probe summary: `codec_name=h264`, 64×64, `avg_frame_rate=12/1`, `nb_frames=4`.

## `t0_vp9.webm` — WebM / VP9

```
ffmpeg -y -hide_banner \
  -f lavfi -i "color=c=red:s=64x64:r=12:d=1" \
  -vf "drawbox=x=48:y=0:w=16:h=16:color=green:t=fill,drawbox=x=0:y=32:w=64:h=32:color=blue:t=fill" \
  -frames:v 4 -c:v libvpx-vp9 -pix_fmt yuv420p -b:v 100k -an \
  tests/video_data/t0_vp9.webm
```

- Codec: VP9, `yuv420p`; container WebM/EBML (`matroska,webm`).
- Probe summary: `codec_name=vp9`, 64×64, `avg_frame_rate=12/1`; the WebM
  container does not state `nb_frames` (reported N/A), the decoder drains 4
  frames.

## Regenerate on Linux

Use the matching prebuilt `third_party/prebuilt/linux64/ffmpeg/bin/ffmpeg`
(or any FFmpeg build with `libopenh264` and `libvpx-vp9`); the commands above
are unchanged except the ffmpeg path.
