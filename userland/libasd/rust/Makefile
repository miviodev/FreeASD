.PHONY: all clean

RUST_TARGET = x86_64-apple-darwin
CRATE_DIR   = asd_sys
OUT_LIB     = ../build/libasd_sys.a

# Locate cargo/rustup: try known Windows path first, fall back to $HOME (Linux/Mac)
_CARGO_SEARCH := /c/Users/Администратор/.cargo/bin/cargo \
                 /c/Users/Administrator/.cargo/bin/cargo \
                 $(HOME)/.cargo/bin/cargo
CARGO  := $(firstword $(wildcard $(_CARGO_SEARCH)))
RUSTUP := $(patsubst %cargo,%rustup,$(CARGO))

all:
	$(RUSTUP) target add $(RUST_TARGET) 2>/dev/null || true
	cd $(CRATE_DIR) && PATH="$$(dirname $(CARGO)):$$PATH" $(CARGO) build --target $(RUST_TARGET) --release
	cp $(CRATE_DIR)/target/$(RUST_TARGET)/release/libasd_sys.a $(OUT_LIB)

clean:
	cd $(CRATE_DIR) && $(CARGO) clean
	rm -f $(OUT_LIB)
