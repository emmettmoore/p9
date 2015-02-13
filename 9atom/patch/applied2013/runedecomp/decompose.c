#include <u.h>
#include <libc.h>
#include <bio.h>

int	flagu;

void
decompose(Biobuf *in, Biobuf *o)
{
	int r, i;
	Rune d[2], c[10];

	for(;;){
		r = Bgetrune(in);
		if(r == Beof)
			return;
		if(runedecompose(r, d) == -1){
			Bputrune(o, r);
			continue;
		}
		for(i = nelem(c)-1; i >= 0; i--){
			r = d[0];
			c[i] = d[1];
			if(runedecompose(r, d) == -1)
				break;
		}
		Bputrune(o, r);
		if(flagu){
			for(; i < nelem(c); i++)
				if(c[i] <= 0xffff)
					Bprint(o, "\\u%.4ux", c[i]);
				else
					Bprint(o, "\\U%.6ux", c[i]);
		}else
			for(; i < nelem(c); i++)
				Bputrune(o, c[i]);
	}
}

void
usage(void)
{
	fprint(2, "usage: decompose [-u] ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	int i, fd;
	Biobuf b, o;

	ARGBEGIN{
	case 'u':
		flagu = 1;
		break;
	default:
		usage();
	}ARGEND
	Binit(&b, 1, OWRITE);
	for(i = 0; i < argc; i++){
		fd = open(argv[i], OREAD);
		if(fd == -1)
			sysfatal("open: %r");
		Binit(&o, fd, OREAD);
		decompose(&o, &b);
		close(fd);
	}
	if(argc == 0){
		Binit(&o, 0, OREAD);
		decompose(&o, &b);
	}
	Bterm(&o);
	Bterm(&b);
	exits("");
}
