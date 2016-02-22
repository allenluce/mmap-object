{
  "targets": [
    {
      "target_name": "<(module_name)",
      "sources": [
        "mmap-object.cc"
      ],
      "cflags_cc": [
        "-Wall",
        "-Werror",
        "-O3",
        "-fexceptions",
        "-frtti"
      ],
      "link_settings": {
        "ldflags": [
          "-L/usr/lib64/boost148"
        ]
      },
      "conditions": [
        [
          "OS=='mac' or '<!(echo $MACHTYPE)'=='x86_64-redhat-linux-gnu'",
          {
            "libraries": [
              "-lboost_system-mt",
              "-lboost_thread-mt"
            ]
          }
        ],
        [
          "OS=='linux' and not '<!(echo $MACHTYPE)'=='x86_64-redhat-linux-gnu'",
          {
            "libraries": [
              "-lboost_system",
              "-lboost_thread"
            ]
          }
        ]
      ],
      "xcode_settings": {
        "OTHER_CFLAGS": [
          "-I/usr/local/opt/boost155/include",
          "-Wno-unused-local-typedefs"
        ],
        "OTHER_LDFLAGS": [
          "-L/usr/local/opt/boost155/lib"
        ],
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "GCC_ENABLE_CPP_RTTI": "-frtti"
      },
      "include_dirs": [
        "<!(node -e \"require('nan')\")",
        "/usr/include/boost148/"
      ]
    },
    {
      "target_name": "action_after_build",
      "type": "none",
      "dependencies": [
        "<(module_name)"
      ],
      "copies": [
        {
          "files": [
            "<(PRODUCT_DIR)/<(module_name).node"
          ],
          "destination": "<(module_path)"
        }
      ]
    }
  ]
}
