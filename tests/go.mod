module lsrp_test

go 1.24.2

require (
	github.com/Pavelavl/go-lsrp v0.0.4
	svgd/tests/shared/benchmark v0.0.0
	svgd/tests/shared/system v0.0.0
)

replace svgd/tests/shared/benchmark => ./shared/benchmark

replace svgd/tests/shared/system => ./shared/system
