with open('bigfile.txt', 'a') as f:
	for i in xrange(10**5):
		f.write(str(i) + "\n")	
