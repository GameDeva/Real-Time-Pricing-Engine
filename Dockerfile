# Multi-stage build for optimal image size
FROM python:3.11-slim as builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source code
COPY . .

# Build C++ library and Python bindings
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --config Release

# Runtime stage
FROM python:3.11-slim

WORKDIR /app

# Copy built artifacts from builder
COPY --from=builder /app/python_app /app/python_app
COPY --from=builder /app/build /app/build

# Install Python dependencies
RUN pip install --no-cache-dir flask flask-cors numpy

# Expose port
EXPOSE 5000

# Set environment variables
ENV PYTHONUNBUFFERED=1
ENV FLASK_APP=python_app/app.py

# Run the Flask app
CMD ["sh", "-c", "python -m flask run --host=0.0.0.0 --port=${PORT:-5000}"]
