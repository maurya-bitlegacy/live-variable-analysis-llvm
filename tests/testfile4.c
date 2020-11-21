int global = 0;
void foo(int x, int y, int z){
	int a = x || y;
	int b = y && z;
	z=global;
}
