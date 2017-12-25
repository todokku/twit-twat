all:
	@echo "for install type 'make install'"

install:
	cp twit-twat.py /usr/local/bin/twit-twat
	cp twit-twat.desktop /usr/local/share/applications/twit-twat.desktop
	cp twit-twat.png /usr/local/share/twit-twat/twit-twat.png
