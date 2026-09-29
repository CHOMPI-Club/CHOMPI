# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: MIT-0

EXPENSIVE = true

PROOF_UID=objectSearch
CHECK_FUNCTION_CONTRACTS = $(PROOF_UID)
HARNESS_FILE= $(PROOF_UID)_harness

USE_DYNAMIC_FRAMES = 1

CBMC_OBJECT_BITS = 11

UNWINDSET += objectSearch.0:$(CBMC_MAX_BUFSIZE)
UNWINDSET += strnEq.0:$(CBMC_MAX_BUFSIZE)

USE_FUNCTION_CONTRACTS += skipAnyScalar
USE_FUNCTION_CONTRACTS += skipCollection
USE_FUNCTION_CONTRACTS += skipSpace
USE_FUNCTION_CONTRACTS += skipString

USE_EXTERNAL_SAT_SOLVER ?= --external-sat-solver cadical

include ../Makefile-json.common

# Substitution command to pass to sed for patching core_json.c. The
# characters " and # must be escaped with backslash.
CORE_JSON_SED_EXPR = s/^static //
