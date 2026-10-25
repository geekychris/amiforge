// version 2: area() changed
#include "shape.h"

Shape::~Shape() {}

Square::Square(int s) : side(s) {}

int Square::area() const
{
    return side * side * 10;
}

void Square::grow() { side++; }
