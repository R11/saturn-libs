# Top-level aggregator. Recurses into each lib's per-lib Makefile.
#
# Saturn cross-compilation lives in individual game projects (the libs
# are consumed as include paths + source files). This file only drives
# the host-side TDD pass.

LIBS := saturn-base saturn-smpc saturn-vdp1 saturn-vdp2 saturn-io saturn-app saturn-bup

.PHONY: test test-tap test-saturn-io clean

# Default target.
test: test-saturn-io
	@for lib in $(LIBS); do \
	    if $(MAKE) -C $$lib -n test >/dev/null 2>&1; then \
	        echo "==> $$lib: make test"; \
	        $(MAKE) -C $$lib test || exit $$?; \
	    else \
	        echo "==> $$lib: no host test target (skipped)"; \
	    fi; \
	done

test-tap: test-saturn-io
	@for lib in $(LIBS); do \
	    if $(MAKE) -C $$lib -n test-tap >/dev/null 2>&1; then \
	        echo "# $$lib"; \
	        $(MAKE) -C $$lib test-tap || exit $$?; \
	    else \
	        echo "# $$lib: no test-tap target (skipped)"; \
	    fi; \
	done

# saturn-io ships a pytest-driven test suite (host C binaries +
# Python bridge). Run it via pytest if available; otherwise skip
# with a notice rather than failing the build.
test-saturn-io:
	@if command -v python3 >/dev/null && python3 -c 'import pytest' >/dev/null 2>&1; then \
	    echo "==> saturn-io: pytest"; \
	    cd saturn-io && python3 -m pytest tests/test_host_binaries.py tools/test_bridge.py -q || exit $$?; \
	else \
	    echo "==> saturn-io: pytest not installed (skipped — install with: pip install pytest)"; \
	fi

clean:
	@for lib in $(LIBS); do \
	    $(MAKE) -C $$lib clean || exit $$?; \
	done
