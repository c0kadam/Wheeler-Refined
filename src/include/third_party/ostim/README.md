# OStimNG Thread API vendoring record

- Upstream repository: https://github.com/VersuchDrei/OStimNG
- Exact revision: `95d9720ee3633896e893127d003dad7375459d50`
- Original path: `skse/src/ModAPI/OstimNG-API-Thread.h`
- Vendored path: `src/include/third_party/ostim/OstimNG-API-Thread.h`
- Upstream license: GNU GPL version 3
- Upstream Git blob: `b633a03359f3baa7c2334bdae0c442f847fa1de8`
- SHA-256: `54ab01b74f4e7fa79f5fde103cdff446de87e7cfa339cf53cef949e378ddb2f8`

The official header is vendored so Wheeler Refined can compile against the
authoritative OStimNG Thread API ABI while continuing to discover the optional
`OStim.dll` interface at runtime. The header is copied byte-for-byte and is
not reformatted or modified.
