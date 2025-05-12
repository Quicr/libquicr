import matplotlib.pyplot as plt


def plot_metrics(df, expressions, separate=False):
    """Plot metrics data"""
    if not separate:
        # Plot everything on one plot
        plt.figure(figsize=(12, 6))
        if df.index.name:  # If we have a named index, iterate through groups
            fig_index = 0
            for idx_val in df.index.unique():
                group_df = df.loc[
                    [idx_val]
                ]  # Keep as DataFrame with double brackets
                for expr in expressions:
                    try:
                        y = eval(
                            expr.replace("df", "group_df")
                        )  # Use group_df in expression
                        label = f"{expr.replace('df[','').replace(']','')} ({df.index.name}={idx_val})"
                    except Exception as e:
                        print(f"Error plotting {expr} for {idx_val}: {e}")
                fig_index = fig_index + 1
                plt.plot(group_df["time"], y, label=label)
            plt.title("Combined Metrics")
            plt.xlabel("Time")
            plt.ylabel("Value")
            plt.legend()
            plt.grid(True)
            plt.show()
        else:
            create_plot(df, expressions, title="Combined Metrics")
    else:
        # Handle separate plots
        if df.index.name:
            for idx_val in df.index.unique():
                group_df = df.loc[
                    [idx_val]
                ]  # Keep as DataFrame with double brackets
                for expr in expressions:
                    expr_mod = expr.replace(
                        "df", "group_df"
                    )  # Modify expression for group
                    create_plot(
                        group_df,
                        [expr_mod],
                        title=f"Expression: {expr} - {df.index.name}={idx_val}",
                    )
        else:
            for expr in expressions:
                create_plot(df, [expr], title=f"Expression: {expr}")


def create_plot(df, expressions, title):
    plt.figure(figsize=(12, 6))

    for expr in expressions:
        try:
            y = eval(expr)
            label = expr.replace("df['", "").replace("']", "")
            plt.plot(df["time"], y, label=label)
        except Exception as e:
            print(f"Error plotting {expr}: {e}")

    plt.title(f"{expressions}")
    plt.xlabel("Time")
    plt.ylabel("Value")
    plt.legend()
    plt.grid(True)
    plt.show()
