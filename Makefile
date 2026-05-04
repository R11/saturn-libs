# Top-level aggregator. Recurses into each lib's per-lib Makefile.
#
# Saturn cross-compilation lives in individual game projects (the libs
# are consumed as include paths + source files). This file only drives
# the host-side TDD pass.

LIBS := saturn-base saturn-smpc saturn-vdp1 saturn-vdp2 saturn-io saturn-app saturn-bup

.PHONY: test test-tap clean

# Default target.
test:
	@for lib in $(LIBS); do \
	    if $(MAKE) -C $$lib -n test >/dev/null 2>&1; then \
	        echo "==> $$lib: make test"; \
	        $(MAKE) -C $$lib test || exit $$?; \
	    else \
	        echo "==> $$lib: no host test target (skipped)"; \
	    fi; \
	done

test-tap:
	@for lib in $(LIBS); do \
	    if $(MAKE) -C $$lib -n test-tap >/dev/null 2>&1; then \
	        echo "# $$lib"; \
	        $(MAKE) -C $$lib test-tap || exit $$?; \
	    else \
	        echo "# $$lib: no test-tap target (skipped)"; \
	    fi; \
	done

clean:
	@for lib in $(LIBS); do \
	    $(MAKE) -C $$lib clean || exit $$?; \
	done
