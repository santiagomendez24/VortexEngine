import ctypes
import mmap
import struct
import time
from concurrent.futures import ThreadPoolExecutor

# Constantes del Protocolo VortexEngine
MAGIC_EXPECTED = 0x564F5254  # 'VORT'
HEADER_STATIC_FORMAT = "<IIII"  # magic (4), slab_size (4), max_slabs (4), header_size (4)

# SharedLogEntryHeader: uint64 (8) + uint32 (4) + uint32 (4) + uint32 (4) + uint64 (8) + uint8 (1) = 29 Bytes
LOG_ENTRY_FORMAT = "<QIIIQB"
LOG_ENTRY_SIZE = struct.calcsize(LOG_ENTRY_FORMAT)


def process_log_entry(
    shm_map: mmap.mmap, entry_bytes: bytes, header_size: int, channel_id: int
) -> int:
    (
        timestamp,
        log_id,
        msg_len,
        msg_offset,
        relative_offset,
        level,
    ) = struct.unpack(LOG_ENTRY_FORMAT, entry_bytes)

    # Cero-Copia: El mensaje de texto arranca justo después de la cabecera de 29 bytes
    msg_start = header_size + relative_offset
    msg_view = memoryview(shm_map)[msg_start : msg_start + msg_len]

    try:
        log_text = bytes(msg_view).decode("utf-8")
    except UnicodeDecodeError:
        log_text = "<Corrupted Payload>"

    total_processed = 0
    start_time = time.time()

    while True:
        # ... extracción binaria ...
        total_processed += 1

        # Imprimir solo un reporte cada 100,000 logs o cada 1 segundo
        if total_processed % 100_000 == 0:
            elapsed = time.time() - start_time
            print(f"Procesados: {total_processed} | Throughput: {total_processed / elapsed:.2f} logs/sec")
        # Devolvemos el avance total en bytes (Cabecera + Payload)
    return LOG_ENTRY_SIZE + msg_len


def consume_private_channel(channel_id: int):
    shm_name = f"Local\\Vortex_Channel_{channel_id}"
    print(f"[Python Worker {channel_id}] Buscando canal IPC '{shm_name}'...")

    # 1. Handshake
    while True:
        try:
            shm_init = mmap.mmap(
                -1, 64, tagname=shm_name, access=mmap.ACCESS_READ
            )
            magic, slab_size, max_slabs, header_size = struct.unpack(
                HEADER_STATIC_FORMAT, shm_init[:16]
            )
            shm_init.close()

            if magic == MAGIC_EXPECTED:
                print(
                    f"[Python Worker {channel_id}] ✅ Conectado | Slabs: {max_slabs} | Slab Size: {slab_size / 1024 / 1024:.2f} MB | Header Size: {header_size} B"
                )
                break
        except Exception:
            pass

        time.sleep(0.2)

    # 2. Mapeo Completo del Anillo
    total_size = header_size + (slab_size * max_slabs)
    shm_map = mmap.mmap(
        -1, total_size, tagname=shm_name, access=mmap.ACCESS_READ
    )

    local_read = 0

    try:
        while True:
            # Leyendo write_offset exacto en el byte 64 debido al alignas(64) de C++
            current_write = struct.unpack("<Q", shm_map[64:72])[0]

            if local_read < current_write:
                total_ring_bytes = slab_size * max_slabs
                entry_offset = header_size + (local_read % total_ring_bytes)

                # Leemos los 29 bytes del SharedLogEntryHeader
                entry_bytes = shm_map[
                    entry_offset : entry_offset + LOG_ENTRY_SIZE
                ]

                # Decodificamos y obtenemos cuántos bytes avanzar (29 + len(msg))
                bytes_consumed = process_log_entry(
                    shm_map, entry_bytes, header_size, channel_id
                )

                local_read += bytes_consumed
            else:
                time.sleep(0.0001)  # Micro-yield

    except KeyboardInterrupt:
        shm_map.close()


def main():
    NUM_WORKERS = 1  # Ajusta al número de canales SPSC de tu ThreadManager

    print(
        f"[Vortex Core] Inicializando {NUM_WORKERS} hilos consumidores SPSC..."
    )

    with ThreadPoolExecutor(max_workers=NUM_WORKERS) as executor:
        for channel_id in range(NUM_WORKERS):
            executor.submit(consume_private_channel, channel_id)


if __name__ == "__main__":
    main()