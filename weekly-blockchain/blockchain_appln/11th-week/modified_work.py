"""
Original work by Aman Abraha Kasa — ALU Week 11.
A tiny program used as the "intellectual property" we protect.
"""

def fibonacci(n):
    a, b = 1, 1
    for _ in range(n):
        a, b = b, a + b
    return a

if __name__ == "__main__":
    print([fibonacci(i) for i in range(10)])
