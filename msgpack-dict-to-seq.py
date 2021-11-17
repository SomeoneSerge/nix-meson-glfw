#! /usr/bin/env nix-shell
#! nix-shell -i python -p "python3.withPackages (ps: with ps; [ numpy msgpack ])"

import numpy as np
import msgpack

from pathlib import Path
import argparse

parser = argparse.ArgumentParser("reshape-msgpack")
parser.add_argument("input", type=Path)
parser.add_argument("--output", "-o", type=Path)

if __name__ == "__main__":
    args = parser.parse_args()

    with open(args.input, "rb") as f:
        msg = msgpack.load(f)

    assert isinstance(msg, dict), type(msg)
    assert "data" in msg, list(msg)
    assert "shape" in msg, list(msg)

    if len(msg["shape"]) == 4:
        assert msg["shape"][0] == 1, msg["shape"]
        msg["shape"] = msg["shape"][1:]

    assert len(msg["shape"]) == 3, msg["shape"]

    data = np.array(msg["data"], dtype=np.float32).reshape(msg["shape"]).transpose(1, 2, 0)

    print(data.shape)
    print(data.nbytes)
    print(data.dtype)

    with open(args.output, "wb") as f:
        msgpack.pack(tuple(data.shape), f)
        msgpack.pack(data.ravel().tolist(), f, use_single_float=True)
