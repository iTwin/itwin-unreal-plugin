set(boostHeaders
	Boost/BoostFusionUtils.h
	Boost/BoostFusionUtils.inl
)
source_group(Boost FILES ${boostHeaders})
set(compilHeaders
	Compil/AlwaysFalse.h
	Compil/Attributes.h
	Compil/AutoOperators.h
	Compil/Break.h
	Compil/EmptyBases.h
	Compil/EnumSwitchCoverage.h
)
source_group(Compil FILES ${compilHeaders})
set(strongTypesHeaders
	StrongTypes/Skills.h
	StrongTypes/TaggedValue.h
	StrongTypes/TaggedValueFW.h
	StrongTypes/TaggedValue_hash.h
	StrongTypes/TaggedValue_io.h
	StrongTypes/TaggedValue_operations.h
	StrongTypes/TaggedValue_traits.h
	StrongTypes/TaggedVector.h
	StrongTypes/TypeOperations.h
)
source_group(StrongTypes FILES ${strongTypesHeaders})
set(utilHeaders
	Util/CleanUpGuard.h
	Util/Enumerations.h
	Util/OptionsClass.h
)
source_group(Util FILES ${utilHeaders})
add_library(BeHeaders INTERFACE ${boostHeaders} ${compilHeaders} ${strongTypesHeaders} ${utilHeaders})
