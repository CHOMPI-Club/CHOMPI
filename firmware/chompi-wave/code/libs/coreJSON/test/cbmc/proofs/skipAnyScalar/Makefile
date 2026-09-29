# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT-0

PROOF_UID=skipAnyScalar
CHECK_FUNCTION_CONTRACTS = $(PROOF_UID)
HARNESS_FILE= $(PROOF_UID)_harness

USE_DYNAMIC_FRAMES = 1

UNWINDSET += skipAnyScalar.0:$(CBMC_MAX_BUFSIZE)
UNWINDSET += strnEq.0:$(CBMC_MAX_BUFSIZE)

USE_FUNCTION_CONTRACTS += skipDigits
USE_FUNCTION_CONTRACTS += skipString

include ../Makefile-json.common

# Substitution command to pass to sed for patching core_json.c. The
# characters " and # must be escaped with backslash.
CORE_JSON_SED_EXPR = s/^static //
