def can_build(env, platform):
    # The renderer adopts the launcher's shared image through the desktop GL interop
    # each platform offers: GL_EXT_memory_object_fd on X11, GL_EXT_memory_object_win32
    # on Windows, IOSurface on macOS.
    return platform in ("x11", "osx", "windows")


def configure(env):
    pass
