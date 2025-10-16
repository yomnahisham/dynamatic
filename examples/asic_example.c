// Simple ASIC example for Dynamatic
// This will be compiled to dataflow circuit and then to ASIC

int add_numbers(int a, int b) {
    return a + b;
}

int multiply_numbers(int a, int b) {
    return a * b;
}

int main() {
    int x = 5;
    int y = 10;
    
    int sum = add_numbers(x, y);
    int product = multiply_numbers(x, y);
    
    return sum + product;
}
