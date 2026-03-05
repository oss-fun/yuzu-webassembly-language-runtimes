FROM scratch
COPY wasm_sqlite_cache_kube.wasm /
COPY opcode_offset_table /
COPY tablemap_func /
COPY tablemap_offset /
COPY type_table /
CMD ["/wasm_sqlite_cache_kube.wasm"]
