import pandas as pd
import re
from datetime import datetime


def parse_csv(filepath, index_col=None, index_values=None):
    """Parse CSV file and convert time to datetime with optional index filtering

    Args:
        filepath: Path to CSV file
        index_col: Column name to use as index
        index_values: List of values to filter index column by
    """
    # First read without parsing dates
    df = pd.read_csv(filepath, sep=",", skipinitialspace=True)

    # Apply index filtering if specified
    if index_col and index_values:
        df = df[df[index_col].isin(index_values)]

    # Set index after filtering
    if index_col:
        df.set_index(index_col, inplace=True)

    print(df.columns.tolist())

    # Clean and convert time column with error handling
    try:
        df["time"] = pd.to_datetime(df["time"], format="%Y-%m-%d %H:%M:%S.%f")
    except ValueError as e:
        print(f"Error parsing time column: {e}")
        print("Attempting alternative time formats...")
        # Try without microseconds
        try:
            df["time"] = pd.to_datetime(df["time"], format="%Y-%m-%d %H:%M:%S")
        except ValueError as e:
            print(f"Failed to parse time column: {e}")
            raise

    return df


def parse_expression(expr):
    """Parse mathematical expression involving column names and nested parentheses"""
    # Remove outer parentheses and whitespace
    expr = expr.strip()
    if expr.startswith("(") and expr.endswith(")"):
        expr = expr[1:-1].strip()

    # Validate expression characters
    valid_chars = re.compile(r"^[\w\s\+\-*/\(\)\.\d]+$")
    if not valid_chars.match(expr):
        raise ValueError(f"Invalid expression: {expr}")

    # Modified regex to better handle numbers and operators
    tokens = re.findall(
        r"""(
            [a-zA-Z][\w_]*(?:\.[a-z]+)?  # Column names with optional pandas operation
            |\d+(?:\.\d+)?                # Integer or decimal numbers
            |[+\-*/()]                    # Operators and parentheses
        )""",
        expr,
        re.VERBOSE,
    )

    print(f"Debug - Tokens: {tokens}")
    expr_safe = ""

    for token in tokens:
        if token in "+-*/()":
            expr_safe += token
        elif token.replace(".", "", 1).isdigit():
            expr_safe += token
        elif "." in token and not token.startswith("."):
            col, op = token.split(".", 1)
            expr_safe += f"df['{col}'].{op}"
        else:
            expr_safe += f"df['{token}']"
        print(f"Debug - Current expr_safe: {expr_safe}")

    return expr_safe
