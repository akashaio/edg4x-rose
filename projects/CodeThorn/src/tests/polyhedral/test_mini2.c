int main()
{
  int i,j,beta;
  int C[3][3];
  int D[3][4];
  beta = 1;
  float floaty=1.0;
  for (i = 1; i < 3; i++)
    for (j = 0; j < 3; j++) {
      D[i][j]=D[i-1][j];
      D[i][j] *= (beta + 1+2+C[1+(i-1)+2][3+j-1+2]+3-4);
      D[i][j] *= (2*(-3)*4/2+beta + 1+2+C[1+(i-1)+2][3+j-1+2]+3-4);
    }
  D[0][1]=1;
  D[0][2]=D[0][1]+D[0][1];
  D[0][3]=D[0][2];
  D[0][4]=D[0][2]+10;
  D[0][4]=D[0][2]+floaty;
  D[0][4]=D[0][4]+C[1][1];
}
