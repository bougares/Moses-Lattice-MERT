CXX = cc

OBJS=MosesGraphReader.o Lattice.o BleuScorer.o Main.o

CXXFLAGS = -Wall -Wfatal-errors -D_FILE_OFFSET_BITS=64 -D_LARGE_FILES 
LDFLAGS =  -lstdc++ -lboost_regex

ifeq ($(DEBUG_OPTION),yes)
CXXFLAGS += -g
LDFLAGS += -g
else
OPTIMIZE_OPTION = yes
endif

ifeq ($(PROFILE_OPTION),yes)
CXXFLAGS += -pg -lc -g
LDFLAGS += -pg -lc -g
OPTIMIZE_OPTION = no
endif

ifeq ($(OPTIMIZE_OPTION),yes)
CXXFLAGS += -O3
else
CXXFLAGS += -O0
endif

LatticeMERT: $(OBJS) Makefile
	@echo "***>" LatticeMERT "<***"
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDFLAGS) -o LatticeMERT  

%.o : %.cpp
	@echo "***" $< "***"
	$(CXX) $(CXXFLAGS) -c $< -o $@  

.PHONY : clean prof debug graph
clean:
	rm -f *.o *~ LatticeMERT

debug: Makefile
	$(MAKE) $(MAKEFILE) DEBUG_OPTION=yes 

prof: Makefile
	$(MAKE) $(MAKEFILE) PROFILE_OPTION=yes

gmon.out: prof
	time ./LatticeMERT > log.txt

graph: gprof2dot.py gmon.out
	gprof ./LatticeMERT | ./gprof2dot.py -s | dot -Tpdf -o callgraph.pdf
