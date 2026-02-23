# ──────────────────────────────────────────────────────────────────────────────
# Stage 1: Build — compile C++ engine + Pybind11 extension
# ──────────────────────────────────────────────────────────────────────────────
FROM python:3.11-slim AS builder

# Build-time deps: CMake, C++20 compiler, OpenSSL dev headers, git (FetchContent)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the full source tree
COPY . .

# Build: cmake configures FetchContent deps then compiles quant_core + quant_pricer .so
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --config Release --parallel $(nproc)

# ──────────────────────────────────────────────────────────────────────────────
# Stage 2: Runtime — lean image that only carries what Flask needs
# ──────────────────────────────────────────────────────────────────────────────
FROM python:3.11-slim

# OpenSSL runtime libs + CA certificate bundle.
# ca-certificates is CRITICAL — without it ixwebsocket's IXHttpClient cannot
# verify Binance's TLS cert (no CA bundle = SSL handshake fails silently, and
# the REST snapshot returns with an error the C++ code treats as a failure).
RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy only what the Flask app needs at runtime:
#   - python_app/   → Flask application code, static HTML/CSS/JS
#   - build/        → quant_pricer.cpython-3.11-*.so + its rpath'd dependencies
COPY --from=builder /app/python_app /app/python_app
COPY --from=builder /app/build      /app/build

# Install Python dependencies from the canonical requirements file.
# gunicorn is added for production-grade serving on Railway.
RUN pip install --no-cache-dir \
    flask>=3.0.0 \
    flask-cors>=4.0.0 \
    requests>=2.31.0 \
    numpy>=1.24.0 \
    gunicorn>=21.0.0

# Tell Python where to find the compiled quant_pricer .so.
# build/ is the cmake output directory where pybind11 places the extension.
ENV PYTHONPATH=/app/build
ENV PYTHONUNBUFFERED=1

# Railway injects $PORT at runtime; default to 5000 for local docker run.
EXPOSE 5000

# Use gunicorn in production (multiple workers, proper signal handling).
# --timeout 120: Binance snapshot + WS connect can take up to 30s per worker.
# --worker-class sync: SSE streaming requires sync (not async) workers.
CMD ["sh", "-c", "gunicorn --bind 0.0.0.0:${PORT:-5000} --workers 2 --threads 4 --timeout 120 --worker-class sync python_app.app:app"]
