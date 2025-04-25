import re

class TableParser:
    def __init__(self):
        # This can be used to store any additional configuration if needed.
        pass
    
    def parse_tables(self, text):
        # Regular expressions to match the table structure and extract data
        table_header_pattern = r'(\+======+\+.+?\+.======\+.+?\+.======+)'
        row_pattern = r'\| (\S+)\|\s*(.+?)\s*\|\'

        # Extract all tables from the text
        matches = re.findall(table_header_pattern, text)
        
        # Convert each match into a dictionary of rows
        tables = []
        for match in matches:
            table_rows = re.findall(row_pattern, match.replace('|', ''))
            header = [item.strip() for item in match.split(' +')[1].split(' +') if item.strip()]
            
            current_table = {}
            for row in table_rows:
                if not row:  # Skip empty lines
                    continue
                row_data = dict(zip(header, (item.strip().replace(' ', '_') for item in row.split(' | '))))
                current_table[row_data['Code']] = row_data
            
            tables.append(current_table)
        
        return tables
