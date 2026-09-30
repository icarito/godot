def can_build(env, platform):
    # slug_svg.cpp parses SVG through the NanoSVG the engine bundles for
    # modules/svg (thirdparty/nanosvg/nanosvg.cc); without that module the
    # nsvgParse symbol is missing and the link fails. Fail loudly here.
    if not env["module_svg_enabled"]:
        print("Slug module requires the 'svg' module (bundled NanoSVG); disabling slug.")
        return False
    return True


def configure(env):
    pass


def get_doc_classes():
    return [
        "SlugFont",
        "SlugLabel3D",
        "SlugVector",
        "SlugVector3D",
    ]


def get_doc_path():
    return "doc_classes"
