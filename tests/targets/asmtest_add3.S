# int add3(int a, int b, int c) — r3 = a + b + c
	.text
	.globl	add3
	.type	add3, @function
add3:
	add	3, 3, 4		# @add3_first
	add	3, 3, 5
	blr
	.size	add3, .-add3
