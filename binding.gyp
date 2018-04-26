{
  "variables": {
    "cflags_cc": [
      "-Wall",
      "-Werror",
      "-O3",
      "-fexceptions",  # Boost on Linux wants this
      "-frtti"         # And this too.
    ],
    "include_dirs": [
      "<!(node -e \"require('nan')\")"
    ],
    "libraries": [], # 
    "OTHER_CFLAGS": [  # for Mac builds
      "-Wno-unused-local-typedefs"
    ]
  },
  "conditions": [
      [
        "OS=='win'", {
          "variables": {
            "include_dirs": [
              "<!(echo %BOOST_ROOT%)"
            ]
          }
        }
      ],
      [
        "OS=='linux'", {
          "variables": {
            "libraries": [
              "-lrt"
            ]
          }
        }
      ],
      
    ],
  "targets": [
    {
      "target_name": "<(module_name)",
      "sources": [ "mmap-object.cc" ],
      "cflags_cc": [ "<@(cflags_cc)" ],
      "include_dirs": [ "<@(include_dirs)" ],
      "libraries": [ "<@(libraries)" ],
        "xcode_settings": {
        "MACOSX_DEPLOYMENT_TARGET": "10.9",
        "OTHER_CFLAGS": [
          "<@(OTHER_CFLAGS)" ,
            "-stdlib=libc++"
        ],
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "GCC_ENABLE_CPP_RTTI": "-frtti"
      },
      "msvs_settings": {
        "VCLinkerTool": {
          "AdditionalLibraryDirectories": [
            "<!(echo %BOOST_ROOT%/stage/lib)"
          ]
        }
      }
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [ "<(module_name)" ],
      "copies": [
        {
          "files": [ "<(PRODUCT_DIR)/<(module_name).node" ],
          "destination": "<(module_path)"
        }
      ]
    }
  ]
}
