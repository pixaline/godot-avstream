import os

def can_build(env, platform):
    return True

def configure(env):
    module_path = os.path.dirname(os.path.abspath(__file__))

    platform_type = env["bits"]
    if (env["platform"] == "android"):
        platform_type = env["android_arch"]
    local_ffmpeg = os.path.join(module_path, "ffmpeg", env["platform"] + "_" + platform_type)

    if os.path.isdir(local_ffmpeg):
        print("av_stream: using local FFmpeg headers from " + local_ffmpeg)
        env.Append(CPPPATH=[os.path.join(local_ffmpeg, "include")])
        env.Append(LIBPATH=[os.path.join(local_ffmpeg, "lib")])

    env.Prepend(LIBS=["vpx", "opus"])
