{
    "targets": [{
        "target_name": "shtatic_decoder",
        "sources": [
            "src/main.cc"
        ],
        'include_dirs': [
            "<!(node -e \"require('nan')\")"
        ],
        "libraries": [
            "-lswresample","-lavformat","-lavfilter","-lavutil","-lavcodec","-lswscale","-lz", "-lm"
        ]
    }]
}
