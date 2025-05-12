import argparse
from metrics_processor.parser import parse_csv, parse_expression
from metrics_processor.plotter import plot_metrics
import re
import sys


def parse_index_arg(index_str):
    """Parse index argument with optional filter values"""
    if not index_str:
        return None, None

    # Match pattern: column_name=["val1", "val2", ...] or column_name["val1", "val2", ...]
    match = re.match(r"(\w+)(?:=)?\[(.*?)\]", index_str)
    if match:
        col_name = match.group(1)
        # Clean up the values string and split
        values_str = match.group(2).strip("\"' ")
        values = [v.strip(' "\'"') for v in values_str.split(",")]
        return col_name, values
    return index_str, None


def main():
    parser = argparse.ArgumentParser(
        description="Plot metrics from CSV files",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Plot single expression
  %(prog)s data.csv "(bytes_published/100)"

  # Plot multiple expressions combined
  %(prog)s data.csv "(bytes_published)" "(objects_published*10)"

  # Filter by index column
  %(prog)s data.csv "(bytes_published)" --index="test_name"

  # Filter by specific index values
  %(prog)s data.csv "(bytes_published)" --index='test_name["TRACK 1", "TRACK 2"]'

  # Create separate plots
  %(prog)s data.csv "(bytes_published)" "(objects_published)" --separate
""",
    )
    parser.add_argument("input_file", help="Input CSV file")
    parser.add_argument(
        "expressions",
        nargs="+",
        help="Expressions to plot. Supports math operations and pandas functions.\n"
        'Examples: "(col1)" "(col2/100)" "(col3.mean*10)"',
    )
    parser.add_argument(
        "--separate",
        action="store_true",
        help="Create separate plot for each expression",
    )
    parser.add_argument(
        "--index",
        help="Column to use as index. Optionally filter values using:\n"
        'column_name["value1", "value2"] or\n'
        'column_name=["value1", "value2"]',
    )

    args = parser.parse_args()

    # Parse index column and filter values
    index_col, index_values = parse_index_arg(args.index)

    # Read and parse data
    df = parse_csv(
        args.input_file, index_col=index_col, index_values=index_values
    )

    # Parse all expressions
    expressions = [parse_expression(expr) for expr in args.expressions]

    # Plot either combined or separately
    plot_metrics(df, expressions, separate=args.separate)


if __name__ == "__main__":
    main()
