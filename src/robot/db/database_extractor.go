package atsf4g_go_robot_db

import (
	extension "github.com/atframework/atsf4g-co/component/private/protocol/extension"
	dbtool "github.com/atframework/robot-go/mode/dbtool"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoreflect"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
)

func init() {
	dbtool.RegisterDatabaseTableExtractor(&ProjectYTableExtractor{})
}

type ProjectYTableExtractor struct{}

func (*ProjectYTableExtractor) ExtractTableIndexes(md protoreflect.MessageDescriptor) []dbtool.TableIndex {
	opts, ok := md.Options().(*descriptorpb.MessageOptions)
	if !ok || opts == nil {
		return nil
	}
	if !proto.HasExtension(opts, extension.E_DatabaseTable) {
		return nil
	}
	tableOpts, ok := proto.GetExtension(opts, extension.E_DatabaseTable).(*extension.DatabaseTableOptions)
	if !ok || tableOpts == nil {
		return nil
	}

	result := make([]dbtool.TableIndex, 0, len(tableOpts.GetIndex()))
	for _, idx := range tableOpts.GetIndex() {
		result = append(result, dbtool.TableIndex{
			Name:          idx.GetName(),
			Type:          dbtool.IndexType(idx.GetType()),
			EnableCAS:     idx.GetEnableCas(),
			MaxListLength: idx.GetMaxListLength(),
			KeyFields:     idx.GetKeyFields(),
		})
	}
	return result
}
