# Software Bill of Materials

The SBOM for each release is generated with Syft and stored in this folder.

Command:

    syft packages . -o spdx-json > sbom/router-$(git rev-parse --short HEAD).spdx.json
