import os
import shlex

from tito.common import info_out, run_command
from tito.tagger import VersionTagger

class MesonVersionTagger(VersionTagger):
    def _set_meson_project_version(self, version):
        run_command('meson rewrite kwargs set project / version %s' % (version))
        run_command('git add -- meson.build')

    def _tag_release(self):
        version = super()._bump_version().split('-', maxsplit=1)[0]

        self._check_tag_does_not_exist(version)
        self._clear_package_metadata()
        self._set_meson_project_version(version)

        metadata_file = os.path.join(self.rel_eng_dir, 'packages', self.project_name)
        with open(metadata_file, 'w') as file:
            file.write('%s %s\n' % (version, self.relative_project_dir))

        files = [
            metadata_file,
            os.path.join(self.full_project_dir, self.spec_file_name),
        ]
        run_command('git add -- %s' % (' '.join([shlex.quote(file) for file in files])))

        message = 'Release version %s' % (version)

        run_command('git commit --message="%s"' % message)
        run_command('git tag --message="%s" %s' % (message, version))

        info_out('%s tagged' % version)
