# Metrics Processor

A Python tool for processing and visualizing CSV-based metrics data. Supports mathematical operations, filtering, and flexible plotting options.

## Installation

1. Clone the repository:
```bash
git clone https://github.com/yourusername/metrics_processor.git
cd metrics_processor
```

2. Create and activate a virtual environment (optional but recommended):
```bash
python -m venv venv
source venv/bin/activate  # Linux/Mac
# or
venv\Scripts\activate  # Windows
```

3. Install dependencies:
```bash
pip install -r requirements.txt
```

## Usage Examples

Basic plotting:
```bash
# Plot a single metric
python -m metrics_processor.main data.csv "(bytes_published)"

# Plot with mathematical operation
python -m metrics_processor.main data.csv "(bytes_published/1000)"
```

Multiple expressions:
```bash
# Plot multiple metrics on same graph
python -m metrics_processor.main data.csv "(bytes_published)" "(objects_published)"

# Plot with calculations
python -m metrics_processor.main data.csv "(bytes_published/objects_published)" "(tx_queue_size*2)"
```

Using pandas operations:
```bash
# Calculate and plot mean values
python -m metrics_processor.main data.csv "(latency.mean)" "(throughput.max)"
```

Filtering by test name:
```bash
# Filter specific tests
python -m metrics_processor.main data.csv "(bytes_published)" --index='test_name["TRACK 1", "TRACK 2"]'

# Plot all tests separately
python -m metrics_processor.main data.csv "(bytes_published)" --index="test_name" --separate
```

## Arguments

- `input_file`: Path to CSV file containing metrics data
- `expressions`: One or more expressions to plot (in parentheses)
- `--separate`: Create separate plots for each expression
- `--index`: Column to use as index with optional value filtering
