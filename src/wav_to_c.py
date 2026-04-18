# import wave
# import numpy as np
# import os
# import sys

# TARGET_RATE =22050 #same rate as code in drum machine

# def process(filepath):
#     with wave.open(filepath,'rb') as wf:
#         n_chan = wf.getnchannels()
#         sample_width = wf.getsampwidth()
#         frame_rate = wf.getframerate()
#         n_frames = wf.getnframes()
#         data = wf.readframes(n_frames)
    
#     if sample_width==2:
#         audio_data = np.frombuffer(data, dtype=np.int16) #16 bit audio
#     else:
#         raise ValueError(f"{filepath}: has to be 16-bit")
#     if n_chan ==2:
#         audio_data = audio_data[::2] 
#         audio_data = audio_data.mean(axis=1).astype(np.int16) #convert to mono by averaging channels
    
#     if frame_rate != TARGET_RATE: #resample target
#         dur = len(audio_data) / frame_rate
#         new_len = int(dur * TARGET_RATE)
#         audio_data = np.interp(
#             np.linspace(0,len(audio_data), new_len),
#             np.arange(len(audio_data)),
#             audio_data
#         ).astype(np.int16)

#         audio_data = ((audio_data.astype(np.int32)+ 32768) >> 1).astype(np.uint16) #convert back to 16 bits
#     return audio_data

# def write(samples, output_c="samples.c", output_h="samples.h"):
#     with open(output_h, "w") as h:
#         h.write("#pragma once\n\n#include <stdint.h>\n\n")
#         for name, data in samples.items():
#             h.write(f"extern const uint16_t {name}_len;\n")
#             h.write(f"extern const uint16_t {name}[];\n\n")
    
#     with open(output_c, "w") as c:
#         c.write('#include "samples.h"\n\n')
#         for name, data in samples.items():
#             c.write(f"const uint16_t {name}[] = {{\n")
#             for i, sample in enumerate(data):
#                 c.write(f"{sample},\n")
#                 if i % 16 ==15:
#                     c.write("\n")
#             c.write("\n};\n")
#             c.write(f"const uint32_t {name}_len = {len(data)};\n\n ")

# def clean(file):
#     name = os.path.splitext(file)[0]
#     name = name.replace("-", "_").replace(" ","_")
#     return name.lower()

# def main(folder):
#     samples = {}
#     for file in os.listdir(folder):
#         if file.lower().endswith(".wav"):
#             path = os.path.join(folder,file)
#             data = process(path)
#             name = clean(file)
#             samples[name] = data
#     write(samples)
#     print("DONE")

# if __name__ == "__main__":
#     if len(sys.argv) < 2:
#         sys.exit(1)
#     main(sys.argv[1])

import wave
import numpy as np
import os
import sys

TARGET_RATE = 22050  # same rate as code in drum machine

def process(filepath):
    with wave.open(filepath, 'rb') as wf:
        n_chan = wf.getnchannels()
        sample_width = wf.getsampwidth()
        frame_rate = wf.getframerate()
        n_frames = wf.getnframes()
        data = wf.readframes(n_frames)

    if sample_width == 2:
        audio_data = np.frombuffer(data, dtype=np.int16)
    else:
        raise ValueError(f"{filepath}: has to be 16-bit")

    # convert to mono by averaging channels
    if n_chan == 2:
        audio_data = audio_data.reshape(-1, 2).mean(axis=1).astype(np.int16)

    # resample to TARGET_RATE if needed
    if frame_rate != TARGET_RATE:
        dur = len(audio_data) / frame_rate
        new_len = int(dur * TARGET_RATE)
        audio_data = np.interp(
            np.linspace(0, len(audio_data) - 1, new_len),
            np.arange(len(audio_data)),
            audio_data
        ).astype(np.int16)

    # convert signed int16 (-32768..32767) -> unsigned uint16 (0..65535)
    # silence = 32768 (midpoint)
    audio_data = (audio_data.astype(np.int32) + 32768).astype(np.uint16)

    return audio_data

def write(samples, output_c="samples.c", output_h="samples.h"):
    with open(output_h, "w") as h:
        h.write("#pragma once\n\n#include <stdint.h>\n\n")
        for name, data in samples.items():
            h.write(f"extern const uint32_t {name}_len;\n")
            h.write(f"extern const uint16_t {name}[];\n\n")

    with open(output_c, "w") as c:
        c.write('#include "samples.h"\n\n')
        for name, data in samples.items():
            c.write(f"const uint16_t {name}[] = {{\n")
            for i, sample in enumerate(data):
                if i % 16 == 0:
                    c.write("    ")
                c.write(f"{sample},")
                if i % 16 == 15:
                    c.write("\n")
            c.write("\n};\n")
            c.write(f"const uint32_t {name}_len = {len(data)};\n\n")

def clean(file):
    name = os.path.splitext(file)[0]
    name = name.replace("-", "_").replace(" ", "_").lower()
    # strip leading digits/underscores so name is a valid C identifier
    name = name.lstrip("0123456789_")
    return name

def main(folder):
    samples = {}
    for file in sorted(os.listdir(folder)):
        if file.lower().endswith(".wav"):
            path = os.path.join(folder, file)
            print(f"  Processing {file}...")
            data = process(path)
            name = clean(file)
            samples[name] = data
            print(f"    -> {name}: {len(data)} samples ({len(data)/TARGET_RATE*1000:.1f} ms, ~{len(data)*2//1024} KB)")
    write(samples)
    total_kb = sum(len(d) * 2 for d in samples.values()) // 1024
    print(f"\nDONE — samples.c + samples.h written")
    print(f"Total flash usage: ~{total_kb} KB / 2048 KB")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python wav_to_c.py <folder>")
        sys.exit(1)
    main(sys.argv[1])        

 