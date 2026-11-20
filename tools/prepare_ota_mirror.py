#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""校验并签发国内 OTA 镜像目录。/ Validate and sign a domestic OTA mirror bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

from prepare_online_update import LAYOUT, PRODUCT, PROJECT, image_metadata


MAGIC = b'IFOTA001'
LIMIT = 4096
CHANNELS = ('stable', 'beta')
KEY_HEADER = Path(__file__).resolve().parents[1] / 'main/ota_mirror_key.h'


def read_manifest(data, channel, image):
    def unique_keys(pairs):
        result = dict(pairs)
        if len(result) != len(pairs):
            raise ValueError('Duplicate manifest key')
        return result

    if not 0 < len(data) <= LIMIT or b'\0' in data:
        raise ValueError(f'Invalid {channel} manifest size or content')
    feed = json.loads(data, object_pairs_hook=unique_keys)
    if not isinstance(feed, dict) or type(feed.get('schema')) is not int or any(
            feed.get(name) != value for name, value in dict(schema=1, channel=channel,
            product=PRODUCT, project=PROJECT, layout=LAYOUT).items()):
        raise ValueError(f'Invalid {channel} manifest identity')
    if feed.get('available') is False and image is None:
        return None
    if feed.get('available') is not True or image is None:
        raise ValueError(f'Expected an image for the available {channel} manifest')
    meta = image_metadata(image, channel)
    git_blob = hashlib.sha1(f'blob {len(image)}\0'.encode() + image).hexdigest()
    if (any(feed.get(name) != value for name, value in meta.items()) or
            feed.get('blob_sha') != git_blob or
            not re.fullmatch(r'[1-9][0-9]{0,19}', str(feed.get('asset_id', ''))) or
            not isinstance(feed.get('asset_id'), str)):
        raise ValueError(f'{channel} image or GitHub asset metadata does not match the manifest')
    return meta['version']


def pinned_public_key():
    point = bytes.fromhex(''.join(re.findall(r'0x([0-9a-fA-F]{2})', KEY_HEADER.read_text())))
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), point)


def sign_pair(manifests, key):
    if (not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(key.curve, ec.SECP256R1) or
            key.public_key().public_numbers() != pinned_public_key().public_numbers()):
        raise ValueError('Signing key does not match the firmware public key')
    body = struct.pack('>8sHH', MAGIC, *(len(data) for data in manifests)) + b''.join(manifests)
    algorithm = ec.ECDSA(hashes.SHA256())
    signature = key.sign(body, algorithm)
    key.public_key().verify(signature, body, algorithm)
    r, s = utils.decode_dss_signature(signature)
    return body + r.to_bytes(32, 'big') + s.to_bytes(32, 'big')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for channel in CHANNELS:
        parser.add_argument(f'--{channel}-manifest', required=True, type=Path)
        parser.add_argument(f'--{channel}-image', type=Path)
    parser.add_argument('--signing-key', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()

    try:
        manifests = [getattr(args, f'{channel}_manifest').read_bytes() for channel in CHANNELS]
        paths = [getattr(args, f'{channel}_image') for channel in CHANNELS]
        images = [path.read_bytes() if path else None for path in paths]
        versions = [read_manifest(data, channel, image)
                    for data, channel, image in zip(manifests, CHANNELS, images)]
        key = serialization.load_pem_private_key(args.signing_key.read_bytes(), password=None)
        files = {'feeds.v1': sign_pair(manifests, key)}
        files.update({f'images/{version}.bin': image for version, image in zip(versions, images)
                      if version is not None})
        args.out.mkdir()
        (args.out / 'images').mkdir()
        for filename, data in files.items():
            (args.out / filename).write_bytes(data)
        (args.out / 'SHA256SUMS').write_text(''.join(
            f'{hashlib.sha256(data).hexdigest()}  {filename}\n' for filename, data in files.items()))
    except (OSError, TypeError, ValueError) as error:
        parser.error(str(error))

    for channel, version in zip(CHANNELS, versions):
        print(f'{channel}: {version or "unavailable"}')


if __name__ == '__main__':
    main()
