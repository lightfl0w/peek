add_requires("quickjs")

add_rules("mode.debug", "mode.release")

target("peek")
    set_kind("binary")
    add_files("src/*.c")
    remove_files("src/gui.c")
    add_packages("quickjs")
    add_syslinks("mbedtls", "mbedx509", "mbedcrypto")
    set_rundir("$(projectdir)")

target("peekg")
    set_kind("binary")
    add_files("src/html.c", "src/dom.c", "src/css.c", "src/render.c", "src/js.c", "src/net.c", "src/util.c", "src/gui.c")
    add_packages("quickjs")
    add_syslinks("mbedtls", "mbedx509", "mbedcrypto", "gtk-3", "gdk-3", "gobject-2.0", "glib-2.0", "gio-2.0", "pangocairo-1.0", "gdk_pixbuf-2.0")
    add_includedirs("/usr/include/gtk-3.0", "/usr/include/glib-2.0", "/usr/lib/glib-2.0/include", "/usr/include/pango-1.0", "/usr/include/cairo", "/usr/include/gdk-pixbuf-2.0", "/usr/include/atk-1.0", "/usr/include/harfbuzz", "/usr/include/libpng16", "/usr/include/freetype2", "/usr/include/fribidi", "/usr/include/pixman-1")
    add_cxflags("-Wno-deprecated-declarations")
    set_rundir("$(projectdir)")

target("check")
    set_kind("phony")
    add_deps("peek")
    on_run(function ()
        os.exec("sh tests/run.sh")
    end)

--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro definition
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--

