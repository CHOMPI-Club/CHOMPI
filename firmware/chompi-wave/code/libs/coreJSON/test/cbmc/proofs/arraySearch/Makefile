# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT-0

PROOF_UID=arraySearch
CHECK_FUNCTION_CONTRACTS = $(PROOF_UID)
HARNESS_FILE= $(PROOF_UID)_harness

USE_DYNAMIC_FRAMES = 1

CBMC_OBJECT_BITS = 12

UNWINDSET += arraySearch.0:$(CBMC_MAX_BUFSIZE)
UNWINDSET += arraySearch.1:$(CBMC_MAX_BUFSIZE)

USE_FUNCTION_CONTRACTS += skipAnyScalar
USE_FUNCTION_CONTRACTS += skipCollection
USE_FUNCTION_CONTRACTS += skipSpace

include ../Makefile-json.common

# Substitution command to pass to sed for patching core_json.c. The
# characters " and # must be escaped with backslash.
CORE_JSON_SED_EXPR = s/^static //
