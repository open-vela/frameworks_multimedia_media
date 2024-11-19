# **Mediatool Tool Description**
[English | [简体中文](./mediatool_zh-cn.md)]

The Mediatool test program is used to test the Media Framework API, which can simulate real-world usage scenarios.

## **Configure the Mediatool**

Configure the CPU to use the mediatool tool, e.g. AP.
```shell
CONFIG_MEDIA=y                        //For cpus that need to use Media, enable this option.
CONFIG_MEDIA_TOOL=y                   //Open the mediatool tool
CONFIG_MEDIA_SERVER_CPUNAME='audio'   //Media-capable cpu name
 ```
 CPUs that provide media capabilities (running mediad), such as AUDIO.
```shell
CONFIG_MEDIA_SERVER=y                         //Media-capable cpu, enable this option
CONFIG_MEDIA_SERVER_CONFIG_PATH="/etc/media/" //Set the directory where media-related configuration files are placed, by default
CONFIG_MEDIA_SERVER_PROGNAME="mediad"         //media deamon program name, default
CONFIG_LIB_FFMPEG=y
CONFIG_LIB_FFMPEG_CONFIGURATION="--disable-sse --enable-avcodec
 --enable-avdevice --enable-avfilter --enable-avformat --enable-decoder='aac,aac_latm,flac,mp3float,pcm_s16le,libopus,libfluoride_sbc,libfluoride_sbc_packed' --enable-demuxer='aac,mp3,pcm_s16le,flac,mov,ogg,wav' --enable-encoder='aac,pcm_s16le,libopus,libfluoride_sbc' --enable-hardcoded-tables --enable-indev=nuttx --enable-ffmpeg --enable-ffprobe --enable-filter='adevsrc,adevsink,afade,amix,amovie_async,amoviesink_async,astats,astreamselect,aresample,volume' --enable-libopus --enable-muxer='opus,opusraw,pcm_s16le,wav' --enable-outdev=bluelet,nuttx --enable-parser='aac,flac' --enable-protocol='cache,file,http,https,rpmsg,tcp,unix' --enable-swresample --tmpdir='/log'"
CONFIG_LIB_PFW=y
```

## **Running Mediatool in the sim environment**
1. Run ap,audio virtual machine:
   ```shell
   sudo ./nuttx
   ```

2. Mount the directory. /music for media files, first mount the host path on the current core (ap):
   ```shell
   ap>mount -t hostfs -o fs=/home/jhd/music /music
   ```
3. Run the mediatool：
   ```shell
   ap>mediatool
   ```

## **Test Methods**

- Play audio files (URL mode):
   ```shell
   open Music
   prepare 0 url music/1.mp3          //Playback in URL mode
   start 0                            //Start playback
   stop 0                             //stop playback
   close 0                            //Close playback
   ```

- Play audio files (Buffer mode):
   ```shell
   open Music
   prepare 0 buffer /music/1.mp3      //Playback in Buffer mode
   start 0
   stop 0
   close 0
   ```

- Record audio files (URL mode):
   ```shell
   copen cap
   prepare 0 url music/2.opus
   start 0                            //Start recording
   stop 0                             //Stop recording
   close 0                            //Close Recording
   ```

- Record Audio Files (Buffer Mode):
   ```shell
   copen cap
   prepare 0 buffer /music/b3.opus format=opus:sample_rate=16000:ch_layout=mono
   start 0
   stop 0
   close 0
   ```

- Playback control:
   ```shell
   pause 0                            //pause playback
   resume 0                           //Resume playback
   seek 0 1000                        //Jump to 1000ms to play
   ```

- Adjust the volume:
   ```shell
   volume 0 50                       //Set the volume to 50%
   ```

- The Mediatool provides debug commands for logging:
   ```shell
   mediatool>dump
   ```