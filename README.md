# falonso-linux
SEGUNDO GII - SISTEMAS OPERATIVOS | En este proyecto se consigue simular, mediante procesos de UNIX, una carrera automovilística. 

## Compilación y Ejecución - Sistemas Linux
- Compilación: gcc -m32 falonso.c libfalonso.a -o falonso
- Ejecución: ./falonso {numero de coches(max.20)} {velocidad(0/1)}

## Dependencias
Las bibliotecas de Linux que se os dan para hacer la práctica de UNIX son bibliotecas de 32 bits. Si en clase o en casa tenéis un Linux de 64 bits, no os van a funcionar a no ser que tengáis preparado el sistema para desarrollo y ejecución de aplicaciones de 32 bits.

El problema radica en que el método para lograr esto último depende de la distribución de Linux que estéis usando. Se describe a continuación el método que se debe seguir en los últimos Ubuntus.

1) Con la orden dpkg --print-architecture, comprobad que realmente tenéis un Linux de 64 bits. Debe aparecer, amd64.
2) Meted ahora la orden dpkg --print-foreign-architectures. Si entre la salida no aparece i386, debéis teclear: sudo dpkg --add-architecture i386
3) Ahora necesitáis tener las bibliotecas de 32 bits también instaladas. Lo lográis con: sudo apt-get install g++-multilib
4) Finalmente, podéis hacer una prueba para ver si todo funciona. Compilad vuestra práctica incluyendo la biblioteca de Linux de 32 bits y la opción -m32 en la línea de compilación del gcc: gcc -m32...
5) Si la fase anterior no dio ningún error y os generó el ejecutable, probad a ejecutarlo. Si todo ha ido bien, debería ejecutarse sin problemas.

Esto es un extracto de la página web de la asignatura. Puede consultar la información completa del proyecto en ella: http://avellano.usal.es/~ssooii/FALONSO/falonsou.htm
