# Third-party notices for PRISM model assets

This package contains model and tokenizer assets from the projects below. The
adjacent license files are included with every staged model package.

## E5 large-v2 encoder and tokenizer

The E5 ONNX encoder weights and tokenizer assets are derived from
[`intfloat/e5-large-v2`](https://huggingface.co/intfloat/e5-large-v2), revision
`f169b11e22de13617baa190a028a32f3493550b6`. The model and tokenizer are
distributed under the MIT License. See `LICENSE-E5-MIT.txt`.

## PRISM retriever and decoder

The PRISM ONNX retriever and decoder are published in the
[`JGolaszewski/PRISM`](https://github.com/JGolaszewski/PRISM) releases. The
source revision used for this export is recorded in
`prism-model-assets.json`. PRISM model weights are distributed under the
Creative Commons Attribution-ShareAlike 4.0 International License. See
`LICENSE-CC-BY-SA-4.0.txt` and the upstream project for attribution and
share-alike terms.

The pinned SHA-256 values and byte sizes in `prism-model-assets.json` identify
the exact files staged by the release workflow. The model files are downloaded
and verified during packaging; they are not stored in this source repository.
